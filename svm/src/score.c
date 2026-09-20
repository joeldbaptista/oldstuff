/*
** score - classify a problem with a trained model.
**
** usage: score [-v] [-n] [-f] [-e eps] [-c cachemb] -t training.sqlite3
**              -p prid -m mid
**
** It reads model mid and problem prid, evaluates the decision function
** at every data point of the problem, turns that into a probability by
** Platt scaling, and writes one yhat row per data point.  The count is
** printed as
**
**     classified=<n>
**
** apply-svm.sh drives this.  README.md describes the database, and
** platt.c the sigmoid and why it is fitted on cross-validated values.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "db.h"
#include "kernel.h"
#include "platt.h"
#include "prob.h"
#include "solver.h"
#include "util.h"

#define EPS 1e-3
#define CACHEMB 100

static void usage(void);
static long toint(const char *s, const char *what);
static double tonum(const char *s, const char *what);
static double decision(const Prob *p, const Model *mo, const double *z);

static void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [-v] [-n] [-f] [-e eps] [-c cachemb]"
	        " -t training.sqlite3 -p prid -m mid\n",
	        argv0 ? argv0 : "score");
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
tonum(const char *s, const char *what)
{
	char *end;
	double v;

	v = strtod(s, &end);
	if (*s == '\0' || *end != '\0')
		die("%s is not a number: %s", what, s);
	return v;
}

/*
** f(z) = sum_i y_i alpha_i K(x_i, z) + b, over the support vectors.
*/
static double
decision(const Prob *p, const Model *mo, const double *z)
{
	double s;
	int i;

	s = mo->bias;
	for (i = 0; i < p->m; i++) {
		if (mo->alpha[i] <= 0.0)
			continue;
		s += p->y[i] * mo->alpha[i] *
		     kernelof(p->kid, p->gamma, p->val + (size_t)i * p->n, z,
		              p->n);
	}
	return s;
}

int
main(int argc, char *argv[])
{
	Prob p;
	Points q;
	Model mo;
	sqlite3 *db;
	const char *path;
	double *dec, *pos;
	double eps, mb, a, b;
	long prid, mid;
	int i, dry, force, verbose;

	argv0 = argv[0];
	path = 0;
	prid = mid = -1;
	eps = EPS;
	mb = CACHEMB;
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
		} else if (!strcmp(argv[i], "-e") && i + 1 < argc) {
			eps = tonum(argv[++i], "eps");
		} else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
			mb = tonum(argv[++i], "cachemb");
		} else {
			usage();
		}
	}
	if (!path || prid < 0 || mid < 0)
		usage();
	if (!(eps > 0.0))
		die("eps must be positive");
	if (!(mb > 0.0))
		die("cachemb must be positive");

	memset(&p, 0, sizeof(p));
	memset(&q, 0, sizeof(q));
	memset(&mo, 0, sizeof(mo));
	db = dbopen(path);
	dbloadmodel(db, (int)mid, &p, &mo);
	dbloadproblem(db, (int)prid, p.n, &q);

	/* The sigmoid is fitted here rather than at training time, since
	** the schema keeps no room for its two parameters. */
	dec = ecalloc((size_t)p.m, sizeof(*dec));
	plattcv(&p, eps, (size_t)(mb * 1024.0 * 1024.0), dec);
	plattfit(&p, dec, &a, &b);
	free(dec);

	pos = ecalloc((size_t)q.m, sizeof(*pos));
	for (i = 0; i < q.m; i++)
		pos[i] = plattprob(decision(&p, &mo, q.val + (size_t)i * q.n),
		                   a, b);

	if (dry) {
		for (i = 0; i < q.m; i++)
			printf("%d|%.17g|%.17g\n", q.dpid[i],
			       decision(&p, &mo, q.val + (size_t)i * q.n),
			       pos[i]);
	} else {
		dbsaveyhat(db, (int)prid, (int)mid, &q, pos, force);
	}
	if (sqlite3_close(db) != SQLITE_OK)
		die("cannot close %s", path);

	if (verbose)
		fprintf(stderr,
		        "%s: mid=%d dsid=%d pid=%d m=%d n=%d points=%d"
		        " A=%.10g B=%.10g\n",
		        argv0, mo.mid, mo.dsid, mo.pid, p.m, p.n, q.m, a, b);
	if (!dry)
		printf("classified=%d\n", q.m);
	return 0;
}
