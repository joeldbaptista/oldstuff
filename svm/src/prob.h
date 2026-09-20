/*
** The training problem, as it is held in memory.
*/

#ifndef PROB_H
#define PROB_H

/* Kernel IDs, as stored in parameters.kid. */
enum {
	KERNEL_LINEAR,
	KERNEL_RADIAL
};

/*
** One dataset joined to one parameter set.  Row i of val holds the n
** variables of data point i, and dpid[i] is the data-point ID that row
** carries in the database.  Data points keep the order of their dpid.
*/
typedef struct prob Prob;
struct prob {
	int m;       /* number of data points */
	int n;       /* number of variables */
	double *val; /* m * n values, row major */
	int *y;      /* labels, -1 or +1 */
	double *w;   /* data point weights */
	double *c;   /* upper bounds, c[i] = w[i] * cost */
	int *dpid;   /* database ID of each row, ascending */
	int kid;     /* kernel ID */
	double cost;
	double gamma; /* ignored when kid is KERNEL_LINEAR */
};

/*
** A set of data points with no labels, which is what a problem is.  Row
** i holds the n variables of data point i, and dpid[i] is the ID that
** row carries in the database.
*/
typedef struct points Points;
struct points {
	int m;       /* number of data points */
	int n;       /* number of variables */
	double *val; /* m * n values, row major */
	int *dpid;   /* database ID of each row, ascending */
};

/* One trained model, read back from the database. */
typedef struct model Model;
struct model {
	int mid;
	int dsid;
	int pid;
	int cid;
	int bid;
	double bias;   /* the b of the decision function */
	double *alpha; /* one per training row, zero where none was stored */
};

#endif /* PROB_H */
