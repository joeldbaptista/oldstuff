/*
** The training set, as it is held in memory.
*/

#ifndef DATA_H
#define DATA_H

/*
** One dataset.  Row i of val holds the n variables of data point i, and
** dpid[i] is the data-point ID that row carries in the database.  Data
** points keep the order of their dpid.  Labels run from 1 to nclass,
** and every one of those labels is carried by at least one row.
*/
typedef struct data Data;
struct data {
	int m;       /* number of data points */
	int n;       /* number of variables */
	int nclass;  /* N, the largest class label */
	double *val; /* m * n values, row major */
	int *y;      /* class labels, 1 to nclass */
	int *dpid;   /* database ID of each row, ascending */
};

#endif /* DATA_H */
