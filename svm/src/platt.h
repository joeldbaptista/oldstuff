/*
** Platt scaling: turning a decision value into a probability.
*/

#ifndef PLATT_H
#define PLATT_H

#include <stddef.h>

#include "prob.h"

#define NFOLD 5

void plattcv(const Prob *p, double eps, size_t cache, double *dec);
void plattfit(const Prob *p, const double *dec, double *a, double *b);
double plattprob(double dec, double a, double b);

#endif /* PLATT_H */
