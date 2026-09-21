/*
** The training problem, as it is held in memory.
*/

#ifndef PROB_H
#define PROB_H

/* Regulariser IDs, as stored in parameters.rid. */
enum {
	REG_NONE,
	REG_L1,
	REG_L2
};

/*
** One dataset joined to one parameter set.  Row i of val holds the n
** variables of data point i, and dpid[i] is the data-point ID that row
** carries in the database.  Data points keep the order of their dpid.
*/
typedef struct prob Prob;
struct prob {
	int m;         /* number of data points */
	int n;         /* number of variables */
	double *val;   /* m * n values, row major */
	int *y;        /* labels, -1 or +1 */
	double *w;     /* data point weights */
	double wsum;   /* sum of the weights */
	int *dpid;     /* database ID of each row, ascending */
	int rid;       /* regulariser ID */
	double lambda; /* regulariser strength */
};

/*
** A set of data points with no labels, which is what a problem is.
*/
typedef struct points Points;
struct points {
	int m;       /* number of data points */
	int n;       /* number of variables */
	double *val; /* m * n values, row major */
	int *dpid;   /* database ID of each row, ascending */
};

/* One fitted model, read back from the database. */
typedef struct model Model;
struct model {
	int mid;
	int dsid;
	int pid;
	int cid;
	int bid;
	int n;
	double *beta; /* one weight per variable */
	double bias;  /* the intercept */
};

#endif /* PROB_H */
