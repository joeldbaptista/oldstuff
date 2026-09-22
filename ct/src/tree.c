/*
** The classification tree.  See tree.h.
**
** A node is cut on one variable at one value.  The rows whose value of
** that variable is at most the cut value go left, and the rest go
** right.  The cut is the one that leaves the two parts as pure as it
** can, purity being measured by the metric the caller chose:
**
**     gini(S)    = 1 - sum_k p_k^2
**     entropy(S) = - sum_k p_k log2 p_k
**
** where p_k is the share of class k in the part.  Both are zero on a
** part of one class and largest when the classes are level, so the two
** agree on what a pure node is and differ only in how hard they
** punish a mixed one.
**
** A candidate is scored by the size weighted impurity of its two
** parts,
**
**     score = (nl * I(left) + nr * I(right)) / n
**
** and the cut with the smallest score wins.  That score can never be
** above the impurity of the node itself, so the difference between the
** two is the gain, and a cut is kept only when the gain is positive.
**
** The search is exhaustive over every variable and every value that
** separates two rows: the node's rows are sorted on one variable, the
** class counts of the left part are carried along the sorted order,
** and each boundary between two distinct values is scored from those
** counts.  So a node costs one sort and one pass per variable, rather
** than one pass per candidate.
**
** Growing stops at a node that is pure, that is smaller than minsplit,
** that sits at maxdepth, or that no cut improves.  There is no
** pruning.
**
** The method is the one of Breiman, Friedman, Olshen and Stone,
** Classification and Regression Trees, Wadsworth, 1984.  Their growing
** rule is what this file implements; the cost complexity pruning of
** their chapter 3 and the surrogate splits they use for a missing
** value are not here.
*/

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "data.h"
#include "tree.h"
#include "util.h"

/*
** A cut whose gain is below this is treated as no cut at all.  The
** scores are sums of a few hundred terms of size one, so anything this
** small is rounding rather than signal.
*/
#define MINGAIN 1e-12

/* A value paired with the row it came from, so a sort keeps the link. */
typedef struct pair Pair;
struct pair {
	double v;
	int i;
};

/* Everything the recursion needs, gathered so it can be passed once. */
typedef struct ctx Ctx;
struct ctx {
	const Data *d;
	Tree *t;
	int *idx;   /* the rows, partitioned in place by the recursion */
	int *part;  /* scratch for reordering a range */
	Pair *pair; /* scratch for the sort */
	int *lcnt;  /* class counts of the left part */
	int *rcnt;  /* class counts of the right part */
};

static int bypair(const void *a, const void *b);
static double impurity(const int *cnt, int nclass, int n, int metric);
static int newnode(Tree *t, int nclass);
static int best(Ctx *c, int lo, int hi, const int *cnt, int *bvar,
                double *bval);
static int build(Ctx *c, int lo, int hi, int depth);

static int
bypair(const void *a, const void *b)
{
	const Pair *x, *y;

	x = a;
	y = b;
	if (x->v < y->v)
		return -1;
	if (x->v > y->v)
		return 1;
	return 0;
}

/*
** The impurity of a part, from its class counts.  Rows that share a
** value are never split apart by the caller, so the order qsort leaves
** ties in does not reach this function and the result is the same on
** every run.
*/
static double
impurity(const int *cnt, int nclass, int n, int metric)
{
	double s, p;
	int k;

	if (n < 1)
		return 0.0;
	s = 0.0;
	for (k = 1; k <= nclass; k++) {
		if (cnt[k] == 0)
			continue;
		p = (double)cnt[k] / n;
		if (metric == METRIC_ENTROPY)
			s -= p * log2(p);
		else
			s += p * p;
	}
	return metric == METRIC_ENTROPY ? s : 1.0 - s;
}

static int
newnode(Tree *t, int nclass)
{
	Node *n;

	if (t->num >= t->max) {
		t->max = t->max ? t->max * 2 : 256;
		t->v = erealloc(t->v, (size_t)t->max * sizeof(*t->v));
	}
	n = &t->v[t->num];
	memset(n, 0, sizeof(*n));
	n->parent = -1;
	n->left = -1;
	n->right = -1;
	n->cutvar = -1;
	n->count = ecalloc((size_t)nclass + 1, sizeof(*n->count));
	return t->num++;
}

/*
** Search every variable and every separating value for the best cut of
** idx[lo..hi), whose class counts are cnt.  Return 1 when a cut with a
** positive gain was found, and 0 when none was.
*/
static int
best(Ctx *c, int lo, int hi, const int *cnt, int *bvar, double *bval)
{
	double score, bscore, il, ir, parent;
	int n, dim, nclass, j, k, cl, nl, nr, found;

	n = hi - lo;
	dim = c->d->n;
	nclass = c->d->nclass;
	parent = impurity(cnt, nclass, n, c->t->metric);
	found = 0;
	bscore = 0.0;

	for (j = 0; j < dim; j++) {
		for (k = 0; k < n; k++) {
			c->pair[k].i = c->idx[lo + k];
			c->pair[k].v =
			        c->d->val[(size_t)c->idx[lo + k] * dim + j];
		}
		qsort(c->pair, (size_t)n, sizeof(*c->pair), bypair);

		for (k = 0; k <= nclass; k++) {
			c->lcnt[k] = 0;
			c->rcnt[k] = cnt[k];
		}
		/* Row k joins the left part, and the boundary after it
		** is then scored.  A boundary inside a run of equal
		** values separates nothing, so it is passed over. */
		for (k = 0; k < n - 1; k++) {
			cl = c->d->y[c->pair[k].i];
			c->lcnt[cl]++;
			c->rcnt[cl]--;
			if (!(c->pair[k].v < c->pair[k + 1].v))
				continue;
			nl = k + 1;
			nr = n - nl;
			if (nl < c->t->minleaf || nr < c->t->minleaf)
				continue;
			il = impurity(c->lcnt, nclass, nl, c->t->metric);
			ir = impurity(c->rcnt, nclass, nr, c->t->metric);
			score = (nl * il + nr * ir) / n;
			if (!found || score < bscore) {
				found = 1;
				bscore = score;
				*bvar = j;
				*bval = (c->pair[k].v + c->pair[k + 1].v) / 2.0;
			}
		}
	}
	return found && parent - bscore > MINGAIN;
}

/*
** Build the node covering idx[lo..hi) and return its index.
*/
static int
build(Ctx *c, int lo, int hi, int depth)
{
	Node *node;
	double cutval;
	int me, dim, nclass, cutvar, i, k, nl, mid;

	dim = c->d->n;
	nclass = c->d->nclass;
	me = newnode(c->t, nclass);
	node = &c->t->v[me];
	node->size = hi - lo;
	node->depth = depth;
	for (i = lo; i < hi; i++)
		node->count[c->d->y[c->idx[i]]]++;
	/* Ties go to the lower class, so the majority is one class and
	** not whichever the loop met last. */
	node->majority = 1;
	for (k = 2; k <= nclass; k++)
		if (node->count[k] > node->count[node->majority])
			node->majority = k;

	if (node->count[node->majority] == node->size)
		return me; /* pure */
	if (node->size < c->t->minsplit)
		return me;
	if (c->t->maxdepth >= 0 && depth >= c->t->maxdepth)
		return me;
	if (!best(c, lo, hi, node->count, &cutvar, &cutval))
		return me;

	/* Reorder the range so that the left part comes first. */
	nl = 0;
	for (i = lo; i < hi; i++)
		if (c->d->val[(size_t)c->idx[i] * dim + cutvar] <= cutval)
			c->part[nl++] = c->idx[i];
	mid = lo + nl;
	for (i = lo; i < hi; i++)
		if (c->d->val[(size_t)c->idx[i] * dim + cutvar] > cutval)
			c->part[nl++] = c->idx[i];
	memcpy(c->idx + lo, c->part, (size_t)(hi - lo) * sizeof(*c->idx));

	c->t->v[me].cutvar = cutvar;
	c->t->v[me].cutval = cutval;

	/* The node array may move under us, so index into it rather
	** than hold a pointer across the recursion. */
	i = build(c, lo, mid, depth + 1);
	c->t->v[me].left = i;
	c->t->v[i].parent = me;
	i = build(c, mid, hi, depth + 1);
	c->t->v[me].right = i;
	c->t->v[i].parent = me;
	return me;
}

Tree *
treebuild(const Data *d, int metric, int minsplit, int minleaf, int maxdepth)
{
	Ctx c;
	Tree *t;
	int i;

	t = ecalloc(1, sizeof(*t));
	t->nclass = d->nclass;
	t->metric = metric;
	t->minsplit = minsplit;
	t->minleaf = minleaf;
	t->maxdepth = maxdepth;

	memset(&c, 0, sizeof(c));
	c.d = d;
	c.t = t;
	c.idx = ecalloc((size_t)d->m, sizeof(*c.idx));
	c.part = ecalloc((size_t)d->m, sizeof(*c.part));
	c.pair = ecalloc((size_t)d->m, sizeof(*c.pair));
	c.lcnt = ecalloc((size_t)d->nclass + 1, sizeof(*c.lcnt));
	c.rcnt = ecalloc((size_t)d->nclass + 1, sizeof(*c.rcnt));
	for (i = 0; i < d->m; i++)
		c.idx[i] = i;

	build(&c, 0, d->m, 0);

	free(c.idx);
	free(c.part);
	free(c.pair);
	free(c.lcnt);
	free(c.rcnt);
	return t;
}

void
treefree(Tree *t)
{
	int i;

	if (!t)
		return;
	for (i = 0; i < t->num; i++)
		free(t->v[i].count);
	free(t->v);
	free(t);
}

int
treeleaves(const Tree *t)
{
	int i, k;

	k = 0;
	for (i = 0; i < t->num; i++)
		if (t->v[i].left < 0)
			k++;
	return k;
}

/*
** The depth of the deepest leaf, counting the root as 0.
*/
int
treedepth(const Tree *t)
{
	int i, max;

	max = 0;
	for (i = 0; i < t->num; i++)
		if (t->v[i].depth > max)
			max = t->v[i].depth;
	return max;
}
