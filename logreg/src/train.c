/*
** Fitting the weighted, regularised logistic regression.  See train.h.
**
** With labels in {-1, +1} and z_i = x_i . beta + b, the fit minimises
**
**     J(beta, b) = (1/W) sum_i w_i log(1 + exp(-y_i z_i)) + lambda R(beta)
**
** where W is the sum of the weights.  Averaging by W rather than summing
** keeps lambda on a scale that does not move with the size of the
** dataset or with how the weights happen to be scaled.  The intercept is
** never penalised, which is the usual convention: penalising it would
** tie the fit to where the origin happens to sit.
**
** R is (1/2)||beta||^2 for L2, ||beta||_1 for L1, and nothing at all for
** REG_NONE.
**
** The method is gradient descent.  The step is 1/L, where L bounds the
** curvature of the smooth part: the Hessian of the averaged loss is
**
**     (1/W) sum_i w_i p_i (1 - p_i) [x_i; 1] [x_i; 1]'
**
** and p(1-p) never exceeds 1/4, so its largest eigenvalue is at most the
** trace, (1/(4W)) sum_i w_i (||x_i||^2 + 1).  For L2 the penalty adds
** lambda.  That step needs no tuning and cannot overshoot.
**
** L1 is the awkward one, because |beta_j| has no derivative at zero.
** The tweak is to leave the penalty out of the gradient and apply it
** afterwards, by the proximal step of the L1 norm, which is the soft
** threshold
**
**     beta_j <- sign(u_j) max(|u_j| - eta lambda, 0)
**
** on u = beta - eta grad(loss).  That is proximal gradient descent, and
** it is the standard answer.  It has two virtues over simply feeding the
** subgradient sign(beta_j) to the same loop: it sets weights to exactly
** zero rather than leaving them to drift near zero, and it keeps the
** convergence of plain gradient descent instead of the slower rate a
** subgradient method settles for.
*/

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "prob.h"
#include "train.h"
#include "util.h"

static double softplus(double t);
static double sigma(double t);
static double lipschitz(const Prob *p);
static double gradient(const Prob *p, const double *beta, double bias,
                       double *gb, double *gbias);

/*
** log(1 + exp(t)), arranged so that exp never overflows.
*/
static double
softplus(double t)
{
	if (t > 0.0)
		return t + log1p(exp(-t));
	return log1p(exp(t));
}

/*
** 1 / (1 + exp(t)), again arranged so that exp never overflows.
*/
static double
sigma(double t)
{
	double e;

	if (t > 0.0) {
		e = exp(-t);
		return e / (1.0 + e);
	}
	return 1.0 / (1.0 + exp(t));
}

/*
** A bound on the curvature of the smooth part, so that a step of 1/L is
** safe.  The trailing 1 is the intercept's own column of ones.
*/
static double
lipschitz(const Prob *p)
{
	const double *row;
	double s, sq;
	int i, j;

	s = 0.0;
	for (i = 0; i < p->m; i++) {
		row = p->val + (size_t)i * p->n;
		sq = 1.0;
		for (j = 0; j < p->n; j++)
			sq += row[j] * row[j];
		s += p->w[i] * sq;
	}
	s /= 4.0 * p->wsum;
	if (p->rid == REG_L2)
		s += p->lambda;
	if (s < 1e-12)
		s = 1e-12;
	return s;
}

/*
** Fill gb and gbias with the gradient of the smooth part and return the
** averaged loss.  For L2 the penalty is smooth, so it goes in here; for
** L1 it does not, and the proximal step deals with it instead.
*/
static double
gradient(const Prob *p, const double *beta, double bias, double *gb,
         double *gbias)
{
	const double *row;
	double z, s, loss, gbb;
	int i, j;

	for (j = 0; j < p->n; j++)
		gb[j] = 0.0;
	gbb = 0.0;
	loss = 0.0;
	for (i = 0; i < p->m; i++) {
		row = p->val + (size_t)i * p->n;
		z = bias;
		for (j = 0; j < p->n; j++)
			z += row[j] * beta[j];
		z *= p->y[i];
		loss += p->w[i] * softplus(-z);
		/* d/dz of the loss at this point, times the weight */
		s = -p->w[i] * p->y[i] * sigma(z);
		for (j = 0; j < p->n; j++)
			gb[j] += s * row[j];
		gbb += s;
	}
	loss /= p->wsum;
	for (j = 0; j < p->n; j++)
		gb[j] /= p->wsum;
	gbb /= p->wsum;

	if (p->rid == REG_L2)
		for (j = 0; j < p->n; j++)
			gb[j] += p->lambda * beta[j];

	*gbias = gbb;
	return loss;
}

/*
** The objective at a given beta and bias.  The averaged loss alone is
** returned through loss when that is not null.
*/
double
trainobj(const Prob *p, const double *beta, double bias, double *loss)
{
	const double *row;
	double z, l, pen;
	int i, j;

	l = 0.0;
	for (i = 0; i < p->m; i++) {
		row = p->val + (size_t)i * p->n;
		z = bias;
		for (j = 0; j < p->n; j++)
			z += row[j] * beta[j];
		l += p->w[i] * softplus(-p->y[i] * z);
	}
	l /= p->wsum;
	if (loss)
		*loss = l;

	pen = 0.0;
	if (p->rid == REG_L1) {
		for (j = 0; j < p->n; j++)
			pen += fabs(beta[j]);
	} else if (p->rid == REG_L2) {
		for (j = 0; j < p->n; j++)
			pen += beta[j] * beta[j];
		pen /= 2.0;
	}
	return l + p->lambda * pen;
}

/*
** Fit the model.  Return 0 when the step became small enough and -1 when
** the iteration cap came first, in which case f still holds the last
** iterate.  With nobias set the intercept is held at zero, which is what
** a comparison against a solver that carries no intercept needs.
*/
int
trainfit(const Prob *p, double tol, long maxiter, int nobias, Fit *f)
{
	double *beta, *gb;
	double bias, gbias, eta, thr, step, d, u, big;
	long it;
	int j, done;

	beta = ecalloc((size_t)p->n, sizeof(*beta));
	gb = ecalloc((size_t)p->n, sizeof(*gb));
	bias = 0.0;
	eta = 1.0 / lipschitz(p);
	thr = p->rid == REG_L1 ? eta * p->lambda : 0.0;

	done = 0;
	for (it = 0; it < maxiter; it++) {
		gradient(p, beta, bias, gb, &gbias);
		step = 0.0;
		big = 1.0;
		for (j = 0; j < p->n; j++) {
			u = beta[j] - eta * gb[j];
			if (p->rid == REG_L1) {
				/* the soft threshold: the proximal step of
				** the L1 norm, and the tweak that gets
				** round |beta| having no derivative at 0 */
				if (u > thr)
					u -= thr;
				else if (u < -thr)
					u += thr;
				else
					u = 0.0;
			}
			d = fabs(u - beta[j]);
			if (d > step)
				step = d;
			beta[j] = u;
			if (fabs(u) > big)
				big = fabs(u);
		}
		if (!nobias) {
			u = bias - eta * gbias;
			d = fabs(u - bias);
			if (d > step)
				step = d;
			bias = u;
			if (fabs(u) > big)
				big = fabs(u);
		}
		if (step <= tol * big) {
			done = 1;
			it++;
			break;
		}
	}

	f->beta = beta;
	f->bias = bias;
	f->obj = trainobj(p, beta, bias, &f->loss);
	f->iter = it;
	f->nnz = 0;
	for (j = 0; j < p->n; j++)
		if (beta[j] != 0.0)
			f->nnz++;
	free(gb);
	return done ? 0 : -1;
}
