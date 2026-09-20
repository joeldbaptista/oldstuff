/*
** Kernel evaluation, with a cache of whole rows.
*/

#ifndef KERNEL_H
#define KERNEL_H

#include <stddef.h>

#include "prob.h"

typedef struct kernel Kernel;

double kernelof(int kid, double gamma, const double *a, const double *b, int n);
Kernel *kernelnew(const Prob *p, size_t bytes);
const double *kernelrow(Kernel *k, int i);
const double *kerneldiag(const Kernel *k);
void kernelfree(Kernel *k);

#endif /* KERNEL_H */
