/*
** The classification tree.
*/

#ifndef TREE_H
#define TREE_H

#include "data.h"

/* Metric IDs, as given to build-ct by -m. */
enum {
	METRIC_GINI,
	METRIC_ENTROPY
};

/*
** One node.  A leaf has no children and no cut, so left, right and
** cutvar are -1 and cutval carries nothing.  The root has no parent, so
** its parent is -1 too.  count is indexed by the class label itself, so
** count[0] is unused and count[k] holds the number of rows of class k.
*/
typedef struct node Node;
struct node {
	int parent;    /* index of the parent, or -1 at the root */
	int left;      /* index of the left child, or -1 at a leaf */
	int right;     /* index of the right child, or -1 at a leaf */
	int cutvar;    /* variable the cut used, or -1 at a leaf */
	double cutval; /* value the cut used */
	int size;      /* data points in the node */
	int majority;  /* most frequent class in the node */
	int depth;     /* 0 at the root */
	int *count;    /* nclass + 1 counts, by class label */
};

/*
** A whole tree.  The root is v[0], and the three limits are the ones
** the recursion stops on.
*/
typedef struct tree Tree;
struct tree {
	Node *v;      /* nodes */
	int num;      /* nodes used */
	int max;      /* nodes allocated */
	int nclass;   /* N, copied from the dataset */
	int metric;   /* METRIC_GINI or METRIC_ENTROPY */
	int minsplit; /* smallest node a cut may be tried on */
	int minleaf;  /* smallest part a cut may leave */
	int maxdepth; /* deepest node allowed, or -1 for no limit */
};

Tree *treebuild(const Data *d, int metric, int minsplit, int minleaf,
                int maxdepth);
void treefree(Tree *t);
int treeleaves(const Tree *t);
int treedepth(const Tree *t);

#endif /* TREE_H */
