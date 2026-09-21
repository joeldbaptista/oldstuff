/*
** Fitting the weighted, regularised logistic regression.
*/

#ifndef TRAIN_H
#define TRAIN_H

#include "prob.h"

/* What one fit produced. */
typedef struct fit Fit;
struct fit {
	double *beta; /* n weights */
	double bias;  /* the intercept */
	double obj;   /* value of the objective */
	double loss;  /* the average loss alone, without the penalty */
	long iter;    /* iterations used */
	int nnz;      /* weights that are not exactly zero */
};

int trainfit(const Prob *p, double tol, long maxiter, int nobias, Fit *f);
double trainobj(const Prob *p, const double *beta, double bias, double *loss);

#endif /* TRAIN_H */
