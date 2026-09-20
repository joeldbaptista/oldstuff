/*
** The SMO solver for the weighted binary SVM.  See solver.h.
**
** It solves the dual of the weighted C-SVC,
**
**     min_a  1/2 a' Q a - e' a
**     s.t.   y' a = 0
**            0 <= a_i <= c_i,   c_i = w_i * cost
**
** where Q_ij = y_i y_j K(x_i, x_j).  Instance weights enter in exactly
** one place, the upper bound c_i, which is why the rest of the method is
** the plain C-SVC one.
**
** The working set holds two variables, which is the smallest set that
** keeps y' a = 0 satisfiable, and that sub-problem has a closed form.
** The pair is chosen by the second order rule of Fan, Chen and Lin, and
** the gradient G_i = sum_j Q_ij a_j - 1 is carried along rather than
** recomputed.  Shrinking is not implemented.
*/

#include <float.h>
#include <math.h>
#include <stdlib.h>

#include "kernel.h"
#include "prob.h"
#include "solver.h"
#include "util.h"

#define TAU 1e-12

static int upper(const Prob *p, const double *a, int i);
static int lower(const double *a, int i);
static int select_(const Prob *p, Kernel *k, const double *a, const double *g,
                   double eps, int *oi, int *oj);
static void step(const Prob *p, Kernel *k, double *a, double *g, int i, int j);
static double rho(const Prob *p, const double *a, const double *g);

static int
upper(const Prob *p, const double *a, int i)
{
	return a[i] >= p->c[i];
}

static int
lower(const double *a, int i)
{
	return a[i] <= 0.0;
}

/*
** Choose the working set.  Variable i maximises -y_i G_i over I_up, and
** variable j is the partner in I_low that promises the largest drop in
** the objective.  Return 1 when the pair already satisfies the stopping
** rule, so that there is nothing left to do.
*/
static int
select_(const Prob *p, Kernel *k, const double *a, const double *g, double eps,
        int *oi, int *oj)
{
	const double *ki, *kd;
	double gmax, gmax2, best, diff, quad, drop;
	int i, j, t;

	gmax = -DBL_MAX;
	gmax2 = -DBL_MAX;
	i = -1;
	j = -1;
	for (t = 0; t < p->m; t++) {
		if (p->y[t] > 0) {
			if (!upper(p, a, t) && -g[t] >= gmax) {
				gmax = -g[t];
				i = t;
			}
		} else {
			if (!lower(a, t) && g[t] >= gmax) {
				gmax = g[t];
				i = t;
			}
		}
	}
	if (i < 0)
		return 1;

	ki = kernelrow(k, i);
	kd = kerneldiag(k);
	best = DBL_MAX;
	for (t = 0; t < p->m; t++) {
		if (p->y[t] > 0) {
			if (lower(a, t))
				continue;
			if (g[t] > gmax2)
				gmax2 = g[t];
			diff = gmax + g[t];
		} else {
			if (upper(p, a, t))
				continue;
			if (-g[t] > gmax2)
				gmax2 = -g[t];
			diff = gmax - g[t];
		}
		if (diff <= 0.0)
			continue;
		quad = kd[i] + kd[t] - 2.0 * ki[t];
		if (quad <= 0.0)
			quad = TAU;
		drop = -(diff * diff) / quad;
		if (drop <= best) {
			best = drop;
			j = t;
		}
	}
	if (gmax + gmax2 < eps || j < 0)
		return 1;

	*oi = i;
	*oj = j;
	return 0;
}

/*
** Solve the two variable sub-problem exactly, clip it into the box that
** c_i and c_j define, then carry the gradient forward.
*/
static void
step(const Prob *p, Kernel *k, double *a, double *g, int i, int j)
{
	const double *ki, *kj, *kd;
	double ci, cj, oi, oj, di, dj, quad, delta, sum;
	int t;

	ki = kernelrow(k, i);
	kj = kernelrow(k, j);
	kd = kerneldiag(k);
	ci = p->c[i];
	cj = p->c[j];
	oi = a[i];
	oj = a[j];

	quad = kd[i] + kd[j] - 2.0 * ki[j];
	if (quad <= 0.0)
		quad = TAU;

	if (p->y[i] != p->y[j]) {
		delta = (-g[i] - g[j]) / quad;
		sum = a[i] - a[j]; /* the difference is what is preserved */
		a[i] += delta;
		a[j] += delta;
		if (sum > 0.0) {
			if (a[j] < 0.0) {
				a[j] = 0.0;
				a[i] = sum;
			}
		} else {
			if (a[i] < 0.0) {
				a[i] = 0.0;
				a[j] = -sum;
			}
		}
		if (sum > ci - cj) {
			if (a[i] > ci) {
				a[i] = ci;
				a[j] = ci - sum;
			}
		} else {
			if (a[j] > cj) {
				a[j] = cj;
				a[i] = cj + sum;
			}
		}
	} else {
		delta = (g[i] - g[j]) / quad;
		sum = a[i] + a[j];
		a[i] -= delta;
		a[j] += delta;
		if (sum > ci) {
			if (a[i] > ci) {
				a[i] = ci;
				a[j] = sum - ci;
			}
		} else {
			if (a[j] < 0.0) {
				a[j] = 0.0;
				a[i] = sum;
			}
		}
		if (sum > cj) {
			if (a[j] > cj) {
				a[j] = cj;
				a[i] = sum - cj;
			}
		} else {
			if (a[i] < 0.0) {
				a[i] = 0.0;
				a[j] = sum;
			}
		}
	}

	di = a[i] - oi;
	dj = a[j] - oj;
	for (t = 0; t < p->m; t++)
		g[t] += p->y[t] * (p->y[i] * ki[t] * di + p->y[j] * kj[t] * dj);
}

/*
** Recover the threshold.  Free multipliers, those strictly inside their
** box, all give the same value in exact arithmetic, so their mean is
** taken.  With no free multiplier the bounded ones only bracket it, and
** the midpoint of that bracket is used.
*/
static double
rho(const Prob *p, const double *a, const double *g)
{
	double ub, lb, sum, yg;
	int i, nfree;

	ub = DBL_MAX;
	lb = -DBL_MAX;
	sum = 0.0;
	nfree = 0;
	for (i = 0; i < p->m; i++) {
		yg = p->y[i] * g[i];
		if (upper(p, a, i)) {
			if (p->y[i] < 0) {
				if (yg < ub)
					ub = yg;
			} else if (yg > lb) {
				lb = yg;
			}
		} else if (lower(a, i)) {
			if (p->y[i] > 0) {
				if (yg < ub)
					ub = yg;
			} else if (yg > lb) {
				lb = yg;
			}
		} else {
			nfree++;
			sum += yg;
		}
	}
	if (nfree > 0)
		return sum / nfree;
	return (ub + lb) / 2.0;
}

/*
** Run the solver.  Return 0 on convergence and -1 when the iteration cap
** is reached first, in which case r still holds the last iterate.
*/
int
smosolve(const Prob *p, Kernel *k, double eps, long maxiter, Result *r)
{
	double *a, *g;
	double v;
	long it;
	int i, j, done;

	a = ecalloc((size_t)p->m, sizeof(*a));
	g = ecalloc((size_t)p->m, sizeof(*g));
	for (i = 0; i < p->m; i++)
		g[i] = -1.0; /* alpha starts at zero, so G = Q.0 - e */

	done = 0;
	for (it = 0; it < maxiter; it++) {
		if (select_(p, k, a, g, eps, &i, &j)) {
			done = 1;
			break;
		}
		step(p, k, a, g, i, j);
	}

	v = 0.0;
	r->nsv = 0;
	r->nbound = 0;
	for (i = 0; i < p->m; i++) {
		v += a[i] * (g[i] - 1.0);
		if (a[i] > 0.0) {
			r->nsv++;
			if (upper(p, a, i))
				r->nbound++;
		}
	}
	r->alpha = a;
	r->obj = v / 2.0;
	r->bias = -rho(p, a, g);
	r->iter = it;
	free(g);
	return done ? 0 : -1;
}
