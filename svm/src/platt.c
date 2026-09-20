/*
** Platt scaling: turning a decision value into a probability.  See
** platt.h.
**
** The decision function returns a signed real, not a probability, so a
** sigmoid
**
**     P(y = +1 | x) = 1 / (1 + exp(A f(x) + B))
**
** is fitted to it.  Fitting on the training decision values alone would
** be circular, because the model already separates those points better
** than it will separate new ones, so the fit uses cross-validated
** decision values instead: the data is cut into NFOLD parts, each part
** is scored by a model trained on the others, and A and B are fitted to
** the pooled result.
**
** The folds are deterministic.  Points are dealt to folds in turn within
** each class, which keeps the class balance of every fold close to that
** of the whole and makes a run reproducible.  LIBSVM shuffles at random
** instead, so its A and B move a little from run to run.
**
** plattfit follows the method of Lin, Weng and Lin, which is Platt's
** with a Newton step, a backtracking line search and targets pulled off
** 0 and 1 to keep the fit from running away.
*/

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "platt.h"
#include "prob.h"
#include "solver.h"
#include "util.h"

#define MAXITER 100
#define MINSTEP 1e-10
#define SIGMA 1e-12
#define FEPS 1e-5
#define MINITER 10000000L

static void subset(const Prob *p, const int *fold, int f, int keep, Prob *out);
static void folds(const Prob *p, int *fold);
static void decfold(const Prob *p, const int *fold, int f, double eps,
                    size_t cache, double *dec);

/*
** Copy the rows whose fold is f, when keep is set, or the rows whose
** fold is not f, when it is clear.
*/
static void
subset(const Prob *p, const int *fold, int f, int keep, Prob *out)
{
	int i, k;

	*out = *p;
	out->m = 0;
	for (i = 0; i < p->m; i++)
		if ((fold[i] == f) == keep)
			out->m++;
	out->val = ecalloc((size_t)out->m * p->n, sizeof(*out->val));
	out->y = ecalloc((size_t)out->m, sizeof(*out->y));
	out->w = ecalloc((size_t)out->m, sizeof(*out->w));
	out->c = ecalloc((size_t)out->m, sizeof(*out->c));
	out->dpid = ecalloc((size_t)out->m, sizeof(*out->dpid));
	k = 0;
	for (i = 0; i < p->m; i++) {
		if ((fold[i] == f) != keep)
			continue;
		memcpy(out->val + (size_t)k * p->n, p->val + (size_t)i * p->n,
		       (size_t)p->n * sizeof(*out->val));
		out->y[k] = p->y[i];
		out->w[k] = p->w[i];
		out->c[k] = p->c[i];
		out->dpid[k] = p->dpid[i];
		k++;
	}
}

/*
** Deal the rows to folds, in turn within each class.
*/
static void
folds(const Prob *p, int *fold)
{
	int i, np, nn;

	np = 0;
	nn = 0;
	for (i = 0; i < p->m; i++) {
		if (p->y[i] > 0)
			fold[i] = np++ % NFOLD;
		else
			fold[i] = nn++ % NFOLD;
	}
}

/*
** Train on every fold but f, then score the rows of fold f into dec.
** A fold whose training part holds a single class cannot be trained, so
** its rows take that class as their decision value.
*/
static void
decfold(const Prob *p, const int *fold, int f, double eps, size_t cache,
        double *dec)
{
	Prob tr;
	Result r;
	Kernel *k;
	double s;
	long maxiter;
	int i, j, np, nn;

	subset(p, fold, f, 0, &tr);
	np = 0;
	nn = 0;
	for (i = 0; i < tr.m; i++) {
		if (tr.y[i] > 0)
			np++;
		else
			nn++;
	}
	if (np == 0 || nn == 0) {
		s = np > 0 ? 1.0 : (nn > 0 ? -1.0 : 0.0);
		for (i = 0; i < p->m; i++)
			if (fold[i] == f)
				dec[i] = s;
		goto done;
	}

	k = kernelnew(&tr, cache);
	maxiter = 100L * tr.m;
	if (maxiter < MINITER)
		maxiter = MINITER;
	if (smosolve(&tr, k, eps, maxiter, &r) < 0)
		die("fold %d did not converge", f);

	for (i = 0; i < p->m; i++) {
		if (fold[i] != f)
			continue;
		s = r.bias;
		for (j = 0; j < tr.m; j++) {
			if (r.alpha[j] <= 0.0)
				continue;
			s += tr.y[j] * r.alpha[j] *
			     kernelof(p->kid, p->gamma,
			              tr.val + (size_t)j * p->n,
			              p->val + (size_t)i * p->n, p->n);
		}
		dec[i] = s;
	}
	free(r.alpha);
	kernelfree(k);
done:
	free(tr.val);
	free(tr.y);
	free(tr.w);
	free(tr.c);
	free(tr.dpid);
}

/*
** Fill dec with a cross-validated decision value for every row of p.
*/
void
plattcv(const Prob *p, double eps, size_t cache, double *dec)
{
	int *fold;
	int f;

	fold = ecalloc((size_t)p->m, sizeof(*fold));
	folds(p, fold);
	for (f = 0; f < NFOLD; f++)
		decfold(p, fold, f, eps, cache, dec);
	free(fold);
}

/*
** Fit A and B so that 1 / (1 + exp(A d + B)) matches the labels.
*/
void
plattfit(const Prob *p, const double *dec, double *a, double *b)
{
	double *t;
	double np, nn, hi, lo;
	double A, B, fval, newA, newB, newf;
	double fab, pp, qq, h11, h22, h21, g1, g2, det, dA, dB, gd, step, d1,
	        d2;
	int i, it;

	np = 0.0;
	nn = 0.0;
	for (i = 0; i < p->m; i++) {
		if (p->y[i] > 0)
			np += 1.0;
		else
			nn += 1.0;
	}
	hi = (np + 1.0) / (np + 2.0);
	lo = 1.0 / (nn + 2.0);
	t = ecalloc((size_t)p->m, sizeof(*t));

	A = 0.0;
	B = log((nn + 1.0) / (np + 1.0));
	fval = 0.0;
	for (i = 0; i < p->m; i++) {
		t[i] = p->y[i] > 0 ? hi : lo;
		fab = dec[i] * A + B;
		if (fab >= 0.0)
			fval += t[i] * fab + log(1.0 + exp(-fab));
		else
			fval += (t[i] - 1.0) * fab + log(1.0 + exp(fab));
	}

	for (it = 0; it < MAXITER; it++) {
		h11 = SIGMA;
		h22 = SIGMA;
		h21 = 0.0;
		g1 = 0.0;
		g2 = 0.0;
		for (i = 0; i < p->m; i++) {
			fab = dec[i] * A + B;
			if (fab >= 0.0) {
				pp = exp(-fab) / (1.0 + exp(-fab));
				qq = 1.0 / (1.0 + exp(-fab));
			} else {
				pp = 1.0 / (1.0 + exp(fab));
				qq = exp(fab) / (1.0 + exp(fab));
			}
			d2 = pp * qq;
			h11 += dec[i] * dec[i] * d2;
			h22 += d2;
			h21 += dec[i] * d2;
			d1 = t[i] - pp;
			g1 += dec[i] * d1;
			g2 += d1;
		}
		if (fabs(g1) < FEPS && fabs(g2) < FEPS)
			break;

		det = h11 * h22 - h21 * h21;
		dA = -(h22 * g1 - h21 * g2) / det;
		dB = -(-h21 * g1 + h11 * g2) / det;
		gd = g1 * dA + g2 * dB;

		for (step = 1.0; step >= MINSTEP; step /= 2.0) {
			newA = A + step * dA;
			newB = B + step * dB;
			newf = 0.0;
			for (i = 0; i < p->m; i++) {
				fab = dec[i] * newA + newB;
				if (fab >= 0.0)
					newf += t[i] * fab +
					        log(1.0 + exp(-fab));
				else
					newf += (t[i] - 1.0) * fab +
					        log(1.0 + exp(fab));
			}
			if (newf < fval + 0.0001 * step * gd) {
				A = newA;
				B = newB;
				fval = newf;
				break;
			}
		}
		if (step < MINSTEP)
			break; /* the line search stalled; keep what we have */
	}
	free(t);
	*a = A;
	*b = B;
}

/*
** The probability that the class is positive.
*/
double
plattprob(double dec, double a, double b)
{
	double f;

	/* Written in two branches so that exp never overflows. */
	f = dec * a + b;
	if (f >= 0.0)
		return exp(-f) / (1.0 + exp(-f));
	return 1.0 / (1.0 + exp(f));
}
