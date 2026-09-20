/*
** Reading the comma-delimited data file.
*/

#ifndef CSV_H
#define CSV_H

/*
** A dense matrix of n rows and dim columns, read from a file.  Row i
** starts at val + i * dim.  A first line that does not parse as numbers
** is taken for a header and skipped.
*/
typedef struct data Data;
struct data {
	int n;       /* rows */
	int dim;     /* columns */
	double *val; /* n * dim values, row major */
};

void csvread(Data *d, const char *path);

#endif /* CSV_H */
