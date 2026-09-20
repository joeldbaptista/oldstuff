/*
** The unsupervised binary partition of idea n. 1.
*/

#ifndef TREE_H
#define TREE_H

#include <stdio.h>

#include "csv.h"

/* How a candidate cut is scored. */
enum {
	CRIT_WEIGHTED, /* the spread of both sides, weighted by their size */
	CRIT_MIN       /* the spread of whichever side is the tighter */
};

/*
** One node.  A leaf has no children and no cut, so left, right and vid
** are -1 and cut, sdleft and sdright carry nothing.  The root has no
** parent, so its parent is -1 too.  The root has no
** parent, so its parent is -1 too.
*/
typedef struct node Node;
struct node {
	int parent;     /* index of the parent, or -1 at the root */
	int left;       /* index of the left child, or -1 */
	int right;      /* index of the right child, or -1 */
	int vid;        /* variable the cut used, or -1 */
	double cut;     /* value the cut used */
	int n;          /* data points in the node */
	double *mean;   /* the mean vector, dim values */
	double sdleft;  /* spread of the left part */
	double sdright; /* spread of the right part */
	int leaf;
};

typedef struct tree Tree;
struct tree {
	Node *v; /* nodes; the root is v[0] */
	int num; /* nodes used */
	int max; /* nodes allocated */
	int dim; /* variables */
};

Tree *treebuild(const Data *d, int nq, int crit);
void treewrite(const Tree *t, FILE *fp);
int treedepth(const Tree *t);

#endif /* TREE_H */
