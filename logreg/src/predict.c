/*
** predict - classify a problem with a fitted logistic regression.
**
** usage: predict [-v] [-n] [-f] -t training.sqlite3 -p prid -m mid
**
** It reads model mid and problem prid, evaluates
**
**     P(y = +1 | x) = 1 / (1 + exp(-(x . beta + b)))
**
** at every data point, and writes one yhat row for each.  The count is
** printed as
**
**     classified=<n>
**
** A logistic regression returns a probability of its own, so nothing has
** to be fitted afterwards to turn a score into one.  That is the one
** place where this is simpler than the sibling svm project, which needs
** Platt scaling.
**
** apply-logreg.sh drives this.
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "db.h"
#include "prob.h"
#include "util.h"

static void usage(void);
static long toint(const char *s, const char *what);
static double score(const Model *mo, const double *z);

static void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [-v] [-n] [-f] -t training.sqlite3 -p prid"
	        " -m mid\n",
	        argv0 ? argv0 : "predict");
	exit(1);
}

static long
toint(const char *s, const char *what)
{
	char *end;
	long v;

	v = strtol(s, &end, 10);
	if (*s == '\0' || *end != '\0')
		die("%s is not an integer: %s", what, s);
	return v;
}

static double
score(const Model *mo, const double *z)
{
	double s;
	int j;

	s = mo->bias;
	for (j = 0; j < mo->n; j++)
		s += mo->beta[j] * z[j];
	return s;
}

int
main(int argc, char *argv[])
{
	Points q;
	Model mo;
	sqlite3 *db;
	const char *path;
	double *pos;
	double z, e;
	long prid, mid;
	int i, dry, force, verbose;

	argv0 = argv[0];
	path = 0;
	prid = mid = -1;
	dry = force = verbose = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if (!strcmp(argv[i], "-n")) {
			dry = 1;
		} else if (!strcmp(argv[i], "-f")) {
			force = 1;
		} else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
			path = argv[++i];
		} else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
			prid = toint(argv[++i], "prid");
		} else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
			mid = toint(argv[++i], "mid");
		} else {
			usage();
		}
	}
	if (!path || prid < 0 || mid < 0)
		usage();

	memset(&q, 0, sizeof(q));
	memset(&mo, 0, sizeof(mo));
	db = dbopen(path);
	dbloadmodel(db, (int)mid, &mo);
	dbloadproblem(db, (int)prid, mo.n, &q);

	pos = ecalloc((size_t)q.m, sizeof(*pos));
	for (i = 0; i < q.m; i++) {
		z = score(&mo, q.val + (size_t)i * q.n);
		/* two branches, so that exp never overflows */
		if (z >= 0.0) {
			pos[i] = 1.0 / (1.0 + exp(-z));
		} else {
			e = exp(z);
			pos[i] = e / (1.0 + e);
		}
	}

	if (dry) {
		for (i = 0; i < q.m; i++)
			printf("%d|%.17g|%.17g\n", q.dpid[i],
			       score(&mo, q.val + (size_t)i * q.n), pos[i]);
	} else {
		dbsaveyhat(db, (int)prid, (int)mid, &q, pos, force);
	}
	if (sqlite3_close(db) != SQLITE_OK)
		die("cannot close %s", path);

	if (verbose)
		fprintf(stderr,
		        "%s: mid=%d dsid=%d pid=%d n=%d points=%d"
		        " bias=%.10g\n",
		        argv0, mo.mid, mo.dsid, mo.pid, mo.n, q.m, mo.bias);
	if (!dry)
		printf("classified=%d\n", q.m);
	return 0;
}
