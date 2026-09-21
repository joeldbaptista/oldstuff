/*
** logreg - fit a weighted, regularised logistic regression held in an
** SQLite training database.
**
** usage: logreg [-v] [-B] [-e tol] [-k maxiter] -t training.sqlite3
**               -d dsid -p pid
**
** It reads dataset dsid under parameter set pid, minimises the averaged
** logistic loss plus the penalty by gradient descent, and writes the
** weights into coefficients and the intercept into bias, each under a
** fresh ID.  The two IDs are printed as
**
**     cid=<n>
**     bid=<n>
**
** -B holds the intercept at zero, which is what comparing against a
** solver that carries no intercept needs.
**
** train-logreg.sh drives this and records the model itself.  README.md
** describes the database, and train.c the method.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "db.h"
#include "prob.h"
#include "train.h"
#include "util.h"

#define TOL 1e-9
#define MAXITER 200000L

static void usage(void);
static long toint(const char *s, const char *what);
static double tonum(const char *s, const char *what);
static const char *regname(int rid);

static void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [-v] [-B] [-e tol] [-k maxiter]"
	        " -t training.sqlite3 -d dsid -p pid\n",
	        argv0 ? argv0 : "logreg");
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

static const char *
regname(int rid)
{
	if (rid == REG_L1)
		return "L1";
	if (rid == REG_L2)
		return "L2";
	return "none";
}

int
main(int argc, char *argv[])
{
	Prob p;
	Fit f;
	sqlite3 *db;
	const char *path;
	double tol;
	long dsid, pid, maxiter;
	int i, cid, bid, nobias, verbose;

	argv0 = argv[0];
	path = 0;
	dsid = pid = -1;
	tol = TOL;
	maxiter = MAXITER;
	nobias = verbose = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if (!strcmp(argv[i], "-B")) {
			nobias = 1;
		} else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
			path = argv[++i];
		} else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
			dsid = toint(argv[++i], "dsid");
		} else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
			pid = toint(argv[++i], "pid");
		} else if (!strcmp(argv[i], "-e") && i + 1 < argc) {
			tol = tonum(argv[++i], "tol");
		} else if (!strcmp(argv[i], "-k") && i + 1 < argc) {
			maxiter = toint(argv[++i], "maxiter");
		} else {
			usage();
		}
	}
	if (!path || dsid < 0 || pid < 0)
		usage();
	if (!(tol > 0.0))
		die("tol must be positive");
	if (maxiter < 1)
		die("maxiter must be at least 1");

	memset(&p, 0, sizeof(p));
	memset(&f, 0, sizeof(f));
	db = dbopen(path);
	dbload(db, (int)dsid, (int)pid, &p);

	if (trainfit(&p, tol, maxiter, nobias, &f) < 0) {
		if (p.rid == REG_NONE)
			die("no convergence after %ld iterations.  With no"
			    " regulariser and data a hyperplane can separate,"
			    " the weights grow without bound and no finite"
			    " optimum exists; give pid %ld an L1 or an L2"
			    " penalty",
			    f.iter, pid);
		die("no convergence after %ld iterations; raise -k or loosen"
		    " -e",
		    f.iter);
	}

	dbsave(db, (int)dsid, &p, &f, &cid, &bid);
	if (sqlite3_close(db) != SQLITE_OK)
		die("cannot close %s", path);

	if (verbose)
		fprintf(stderr,
		        "%s: m=%d n=%d reg=%s lambda=%g tol=%g iter=%ld"
		        " obj=%.10g loss=%.10g bias=%.10g nnz=%d/%d\n",
		        argv0, p.m, p.n, regname(p.rid), p.lambda, tol, f.iter,
		        f.obj, f.loss, f.bias, f.nnz, p.n);
	printf("cid=%d\n", cid);
	printf("bid=%d\n", bid);
	return 0;
}
