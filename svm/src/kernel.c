/*
** Kernel evaluation, with a cache of whole rows.  See kernel.h.
**
** Row i holds K(i,t) for every t, so it is independent of the labels.
** Rows are kept on a least-recently-used list and the tail is dropped
** when the budget is exhausted.  The solver holds two rows at once, so
** the cache always keeps room for at least two: a row just handed out
** sits at the head of the list and cannot be the next one evicted.
*/

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include "kernel.h"
#include "prob.h"
#include "util.h"

typedef struct entry Entry;
struct entry {
	double *v;   /* the row, or 0 when it is not resident */
	Entry *prev; /* towards the most recently used */
	Entry *next; /* towards the least recently used */
};

struct kernel {
	const Prob *p;
	double *sq;   /* squared norms, for the radial kernel */
	double *diag; /* K(i,i) */
	Entry *e;     /* one entry per row */
	Entry head;   /* sentinel of the use list */
	int live;     /* rows resident */
	int max;      /* rows the budget allows */
};

static double dot(const Kernel *k, int i, int j);
static double eval(const Kernel *k, int i, int j);
static void unlink_(Entry *e);
static void tofront(Kernel *k, Entry *e);
static void fill(Kernel *k, int i, double *v);

static double
dot(const Kernel *k, int i, int j)
{
	const double *a, *b;
	double s;
	int t, n;

	n = k->p->n;
	a = k->p->val + (size_t)i * n;
	b = k->p->val + (size_t)j * n;
	s = 0.0;
	for (t = 0; t < n; t++)
		s += a[t] * b[t];
	return s;
}

static double
eval(const Kernel *k, int i, int j)
{
	double d;

	if (k->p->kid == KERNEL_LINEAR)
		return dot(k, i, j);
	d = k->sq[i] + k->sq[j] - 2.0 * dot(k, i, j);
	if (d < 0.0)
		d = 0.0;
	return exp(-k->p->gamma * d);
}

static void
unlink_(Entry *e)
{
	if (!e->prev)
		return;
	e->prev->next = e->next;
	e->next->prev = e->prev;
	e->prev = e->next = 0;
}

static void
tofront(Kernel *k, Entry *e)
{
	unlink_(e);
	e->next = k->head.next;
	e->prev = &k->head;
	k->head.next->prev = e;
	k->head.next = e;
}

static void
fill(Kernel *k, int i, double *v)
{
	int t;

	for (t = 0; t < k->p->m; t++)
		v[t] = eval(k, i, t);
}

/*
** Evaluate the kernel between two rows of n variables that need not
** belong to the same problem.  Scoring uses this, because a point to be
** classified is not part of the training set.
*/
double
kernelof(int kid, double gamma, const double *a, const double *b, int n)
{
	double s, d, t;
	int i;

	if (kid == KERNEL_LINEAR) {
		s = 0.0;
		for (i = 0; i < n; i++)
			s += a[i] * b[i];
		return s;
	}
	d = 0.0;
	for (i = 0; i < n; i++) {
		t = a[i] - b[i];
		d += t * t;
	}
	return exp(-gamma * d);
}

/*
** Build a kernel over p, allowing bytes of row cache.  The budget is
** raised when it cannot hold two rows, because the solver needs two.
*/
Kernel *
kernelnew(const Prob *p, size_t bytes)
{
	Kernel *k;
	size_t row;
	int i;

	k = ecalloc(1, sizeof(*k));
	k->p = p;
	k->e = ecalloc((size_t)p->m, sizeof(*k->e));
	k->head.next = &k->head;
	k->head.prev = &k->head;

	row = (size_t)p->m * sizeof(double);
	k->max = (int)(bytes / row);
	if (k->max < 2)
		k->max = 2;
	if (k->max > p->m)
		k->max = p->m;

	if (p->kid == KERNEL_RADIAL) {
		k->sq = ecalloc((size_t)p->m, sizeof(*k->sq));
		for (i = 0; i < p->m; i++)
			k->sq[i] = dot(k, i, i);
	}
	k->diag = ecalloc((size_t)p->m, sizeof(*k->diag));
	for (i = 0; i < p->m; i++)
		k->diag[i] = eval(k, i, i);
	return k;
}

/*
** Return row i, computing it if the cache does not hold it.  The row
** stays valid until the second call after this one.
*/
const double *
kernelrow(Kernel *k, int i)
{
	Entry *e, *old;

	e = &k->e[i];
	if (e->v) {
		tofront(k, e);
		return e->v;
	}
	while (k->live >= k->max) {
		old = k->head.prev;
		unlink_(old);
		free(old->v);
		old->v = 0;
		k->live--;
	}
	e->v = ecalloc((size_t)k->p->m, sizeof(*e->v));
	k->live++;
	tofront(k, e);
	fill(k, i, e->v);
	return e->v;
}

const double *
kerneldiag(const Kernel *k)
{
	return k->diag;
}

/*
** Release a kernel and every row it still holds.
*/
void
kernelfree(Kernel *k)
{
	int i;

	if (!k)
		return;
	for (i = 0; i < k->p->m; i++)
		free(k->e[i].v);
	free(k->e);
	free(k->sq);
	free(k->diag);
	free(k);
}
