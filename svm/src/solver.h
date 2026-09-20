/*
** The SMO solver for the weighted binary SVM.
*/

#ifndef SOLVER_H
#define SOLVER_H

#include "kernel.h"
#include "prob.h"

/* What one run of the solver produced. */
typedef struct result Result;
struct result {
	double *alpha; /* m multipliers, each in [0, c[i]] */
	double bias;   /* the b of f(x) = sum_i y_i alpha_i K(x_i,x) + b */
	double obj;    /* value of the dual objective */
	long iter;     /* iterations used */
	int nsv;       /* multipliers strictly above zero */
	int nbound;    /* multipliers at their upper bound */
};

int smosolve(const Prob *p, Kernel *k, double eps, long maxiter, Result *r);

#endif /* SOLVER_H */
