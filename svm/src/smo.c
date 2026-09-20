/*
** smo - train a weighted binary SVM held in an SQLite training database.
**
** usage: smo [-v] [-e eps] [-c cachemb] -t training.sqlite3 -d dsid
**            -p pid
**
** It reads dataset dsid under parameter set pid, solves the dual by
** SMO, and writes the multipliers into coefficients and the threshold
** into bias, each under a fresh ID.  The two IDs are printed as
**
**     cid=<n>
**     bid=<n>
**
** train-svm.sh drives this and records the model itself.  README.md
** describes the database, and solver.c the method.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "db.h"
#include "kernel.h"
#include "prob.h"
#include "solver.h"
#include "util.h"

#define EPS 1e-3
#define CACHEMB 100
#define MINITER 10000000L

static void usage(void);
static long toint(const char *s, const char *what);
static double tonum(const char *s, const char *what);

static void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [-v] [-e eps] [-c cachemb] -t training.sqlite3"
	        " -d dsid -p pid\n",
	        argv0 ? argv0 : "smo");
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

int
main(int argc, char *argv[])
{
	Prob p;
	Result r;
	Kernel *k;
	sqlite3 *db;
	const char *path;
	double eps, mb;
	long dsid, pid, maxiter;
	int i, cid, bid, verbose, ok;

	argv0 = argv[0];
	path = 0;
	dsid = pid = -1;
	eps = EPS;
	mb = CACHEMB;
	verbose = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
			path = argv[++i];
		} else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
			dsid = toint(argv[++i], "dsid");
		} else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
			pid = toint(argv[++i], "pid");
		} else if (!strcmp(argv[i], "-e") && i + 1 < argc) {
			eps = tonum(argv[++i], "eps");
		} else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
			mb = tonum(argv[++i], "cachemb");
		} else {
			usage();
		}
	}
	if (!path || dsid < 0 || pid < 0)
		usage();
	if (!(eps > 0.0))
		die("eps must be positive");
	if (!(mb > 0.0))
		die("cachemb must be positive");

	memset(&p, 0, sizeof(p));
	memset(&r, 0, sizeof(r));
	db = dbopen(path);
	dbload(db, (int)dsid, (int)pid, &p);

	k = kernelnew(&p, (size_t)(mb * 1024.0 * 1024.0));
	maxiter = 100L * p.m;
	if (maxiter < MINITER)
		maxiter = MINITER;
	ok = smosolve(&p, k, eps, maxiter, &r);
	if (ok < 0)
		die("no convergence after %ld iterations; raise eps or check"
		    " the parameters",
		    r.iter);

	dbsave(db, (int)dsid, &p, &r, &cid, &bid);
	if (sqlite3_close(db) != SQLITE_OK)
		die("cannot close %s", path);

	if (verbose)
		fprintf(stderr,
		        "%s: m=%d n=%d kid=%d cost=%g gamma=%g eps=%g"
		        " iter=%ld obj=%.10g rho=%.10g nsv=%d nbound=%d\n",
		        argv0, p.m, p.n, p.kid, p.cost, p.gamma, eps, r.iter,
		        r.obj, -r.bias, r.nsv, r.nbound);
	printf("cid=%d\n", cid);
	printf("bid=%d\n", bid);
	return 0;
}
