/*
** The unsupervised binary partition of idea n. 1.  See tree.h.
**
** A cut is chosen without any label.  For each variable the node's
** values of that variable are sorted, and a cut is tried at each tenth
** of the way along, so at 10 per cent, 20 per cent and so on.  Each
** candidate is scored by how tightly its two parts sit in the feature
** space, and the best is kept.  The partition then repeats until every
** data point is alone in a leaf.
**
** The spread of a part is the root mean square of the per variable
** standard deviations,
**
**     spread(S) = sqrt( (1/dim) sum_j var_j(S) )
**
** so it is one number describing how tight the part is across all
** variables at once, not only along the variable that was cut.
**
** Two scores are available.  CRIT_WEIGHTED takes the spread of both
** sides weighted by their size, which is the usual variance reduction
** and does not care which side is the tight one.  CRIT_MIN takes the
** spread of the tighter side alone, which is the literal reading of
** "the left part or the right part is very concentrated"; it has a bias
** towards peeling off the smallest part it is offered, because few
** points are tight almost by definition.
**
** Candidate cuts are evaluated from prefix sums taken along the sorted
** order, so a node costs O(n * dim * dim) rather than one pass per
** candidate.
*/

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "csv.h"
#include "tree.h"
#include "util.h"

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
	int nq;
	int crit;
	int *idx;      /* the rows, partitioned in place by the recursion */
	Pair *pair;    /* scratch for the sort */
	double *sum;   /* prefix sums, (n + 1) * dim */
	double *sumsq; /* prefix sums of squares, (n + 1) * dim */
	int *part;     /* scratch for reordering a range */
};

static int bypair(const void *a, const void *b);
static double spread(const double *sum, const double *sumsq, int cnt, int dim);
static int newnode(Tree *t, int dim);
static void meanof(Ctx *c, int lo, int hi, double *out);
static int best(Ctx *c, int lo, int hi, int *bvid, double *bcut, double *bsl,
                double *bsr);
static int build(Ctx *c, int lo, int hi);

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
** The spread of a part, from its running sums.  A part of one point has
** no spread at all.
*/
static double
spread(const double *sum, const double *sumsq, int cnt, int dim)
{
	double s, m, var;
	int j;

	if (cnt < 2)
		return 0.0;
	s = 0.0;
	for (j = 0; j < dim; j++) {
		m = sum[j] / cnt;
		var = sumsq[j] / cnt - m * m;
		if (var > 0.0)
			s += var;
	}
	return sqrt(s / dim);
}

static int
newnode(Tree *t, int dim)
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
	n->vid = -1;
	n->mean = ecalloc((size_t)dim, sizeof(*n->mean));
	return t->num++;
}

static void
meanof(Ctx *c, int lo, int hi, double *out)
{
	const double *row;
	int i, j, dim;

	dim = c->d->dim;
	for (j = 0; j < dim; j++)
		out[j] = 0.0;
	for (i = lo; i < hi; i++) {
		row = c->d->val + (size_t)c->idx[i] * dim;
		for (j = 0; j < dim; j++)
			out[j] += row[j];
	}
	for (j = 0; j < dim; j++)
		out[j] /= hi - lo;
}

/*
** Search every variable and every quantile for the best cut.  Return 1
** when one was found, and 0 when no cut separates the points at all,
** which happens once they are identical.
*/
static int
best(Ctx *c, int lo, int hi, int *bvid, double *bcut, double *bsl, double *bsr)
{
	const double *row, *lsum, *lsq;
	double *rsum, *rsq;
	double sl, sr, score, bscore, cut;
	int n, dim, j, k, q, at, found;

	n = hi - lo;
	dim = c->d->dim;
	found = 0;
	bscore = 0.0;
	rsum = ecalloc((size_t)dim, sizeof(*rsum));
	rsq = ecalloc((size_t)dim, sizeof(*rsq));

	for (j = 0; j < dim; j++) {
		for (k = 0; k < n; k++) {
			c->pair[k].i = c->idx[lo + k];
			c->pair[k].v =
			        c->d->val[(size_t)c->idx[lo + k] * dim + j];
		}
		qsort(c->pair, (size_t)n, sizeof(*c->pair), bypair);

		/* Prefix sums along the sorted order, over every
		** variable, so each candidate costs O(dim). */
		for (k = 0; k < dim; k++) {
			c->sum[k] = 0.0;
			c->sumsq[k] = 0.0;
		}
		for (k = 0; k < n; k++) {
			row = c->d->val + (size_t)c->pair[k].i * dim;
			for (q = 0; q < dim; q++) {
				c->sum[(size_t)(k + 1) * dim + q] =
				        c->sum[(size_t)k * dim + q] + row[q];
				c->sumsq[(size_t)(k + 1) * dim + q] =
				        c->sumsq[(size_t)k * dim + q] +
				        row[q] * row[q];
			}
		}

		for (q = 1; q < c->nq; q++) {
			at = (int)((double)q * n / c->nq);
			if (at < 1 || at > n - 1)
				continue;
			/* A tie across the boundary separates nothing. */
			if (!(c->pair[at - 1].v < c->pair[at].v))
				continue;
			for (k = 0; k < dim; k++) {
				rsum[k] = c->sum[(size_t)n * dim + k] -
				          c->sum[(size_t)at * dim + k];
				rsq[k] = c->sumsq[(size_t)n * dim + k] -
				         c->sumsq[(size_t)at * dim + k];
			}
			lsum = c->sum + (size_t)at * dim;
			lsq = c->sumsq + (size_t)at * dim;
			sl = spread(lsum, lsq, at, dim);
			sr = spread(rsum, rsq, n - at, dim);
			if (c->crit == CRIT_MIN)
				score = sl < sr ? sl : sr;
			else
				score = (at * sl + (n - at) * sr) / n;
			if (!found || score < bscore) {
				found = 1;
				bscore = score;
				cut = (c->pair[at - 1].v + c->pair[at].v) / 2.0;
				*bvid = j;
				*bcut = cut;
				*bsl = sl;
				*bsr = sr;
			}
		}
	}
	free(rsum);
	free(rsq);
	return found;
}

/*
** Build the node covering idx[lo..hi) and return its index.
*/
static int
build(Ctx *c, int lo, int hi)
{
	Node *node;
	double cut, sl, sr;
	int me, vid, i, mid, dim, nl;

	dim = c->d->dim;
	me = newnode(c->t, dim);
	c->t->v[me].n = hi - lo;
	meanof(c, lo, hi, c->t->v[me].mean);

	if (hi - lo < 2 || !best(c, lo, hi, &vid, &cut, &sl, &sr)) {
		c->t->v[me].leaf = 1;
		return me;
	}

	/* Reorder the range so that the left part comes first. */
	nl = 0;
	for (i = lo; i < hi; i++)
		if (c->d->val[(size_t)c->idx[i] * dim + vid] <= cut)
			c->part[nl++] = c->idx[i];
	mid = lo + nl;
	for (i = lo; i < hi; i++)
		if (c->d->val[(size_t)c->idx[i] * dim + vid] > cut)
			c->part[nl++] = c->idx[i];
	memcpy(c->idx + lo, c->part, (size_t)(hi - lo) * sizeof(*c->idx));

	node = &c->t->v[me];
	node->vid = vid;
	node->cut = cut;
	node->sdleft = sl;
	node->sdright = sr;

	/* The array may move under us, so index rather than hold a
	** pointer across the recursion. */
	i = build(c, lo, mid);
	c->t->v[me].left = i;
	c->t->v[i].parent = me;
	i = build(c, mid, hi);
	c->t->v[me].right = i;
	c->t->v[i].parent = me;
	return me;
}

Tree *
treebuild(const Data *d, int nq, int crit)
{
	Ctx c;
	Tree *t;
	int i;

	t = ecalloc(1, sizeof(*t));
	t->dim = d->dim;

	memset(&c, 0, sizeof(c));
	c.d = d;
	c.t = t;
	c.nq = nq;
	c.crit = crit;
	c.idx = ecalloc((size_t)d->n, sizeof(*c.idx));
	c.part = ecalloc((size_t)d->n, sizeof(*c.part));
	c.pair = ecalloc((size_t)d->n, sizeof(*c.pair));
	c.sum = ecalloc((size_t)(d->n + 1) * d->dim, sizeof(*c.sum));
	c.sumsq = ecalloc((size_t)(d->n + 1) * d->dim, sizeof(*c.sumsq));
	for (i = 0; i < d->n; i++)
		c.idx[i] = i;

	build(&c, 0, d->n);

	free(c.idx);
	free(c.part);
	free(c.pair);
	free(c.sum);
	free(c.sumsq);
	return t;
}

/*
** Write the tree, one row per node.  The row number gives the node its
** index, so the root is the first row after the header.
*/
void
treewrite(const Tree *t, FILE *fp)
{
	const Node *n;
	int i, j;

	fprintf(fp, "parent,left,right,vid,cut,n");
	for (j = 0; j < t->dim; j++)
		fprintf(fp, ",m%d", j + 1);
	fprintf(fp, ",sd_left,sd_right\n");

	for (i = 0; i < t->num; i++) {
		n = &t->v[i];
		fprintf(fp, "%d,%d,%d,%d,", n->parent, n->left, n->right,
		        n->vid);
		if (n->leaf)
			fprintf(fp, ",%d", n->n);
		else
			fprintf(fp, "%.10g,%d", n->cut, n->n);
		for (j = 0; j < t->dim; j++)
			fprintf(fp, ",%.10g", n->mean[j]);
		if (n->leaf)
			fprintf(fp, ",,\n");
		else
			fprintf(fp, ",%.10g,%.10g\n", n->sdleft, n->sdright);
	}
}

/*
** The depth of the deepest leaf, counting the root as 0.
*/
int
treedepth(const Tree *t)
{
	int *d;
	int i, max;

	d = ecalloc((size_t)t->num, sizeof(*d));
	max = 0;
	for (i = 0; i < t->num; i++) {
		if (t->v[i].left >= 0)
			d[t->v[i].left] = d[i] + 1;
		if (t->v[i].right >= 0)
			d[t->v[i].right] = d[i] + 1;
		if (d[i] > max)
			max = d[i];
	}
	free(d);
	return max;
}
