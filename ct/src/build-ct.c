/*
** build-ct - grow a classification tree from an SQLite training
** database.
**
** usage: build-ct [-v] [-f] [-d dsid] [-s minsplit] [-l minleaf]
**                 [-D maxdepth] -i training.sqlite3 -o tree.sqlite3
**                 -m metric
**
** It reads one dataset, cuts it until no cut is left to make, and
** writes the tree into a file of its own, holding the tables ct and
** node_distribution.  The metric is 0 for the Gini index and 1 for
** entropy.  The counts are printed as
**
**     nodes=<n>
**     leaves=<n>
**
** README.md describes the database, and tree.c the method.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "data.h"
#include "db.h"
#include "tree.h"
#include "util.h"

#define MINSPLIT 2
#define MINLEAF 1

static void usage(void);
static long toint(const char *s, const char *what);

static void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [-v] [-f] [-d dsid] [-s minsplit] [-l minleaf]"
	        " [-D maxdepth]\n"
	        "       -i training.sqlite3 -o tree.sqlite3 -m metric\n",
	        argv0 ? argv0 : "build-ct");
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

int
main(int argc, char *argv[])
{
	Data d;
	Tree *t;
	sqlite3 *db;
	const char *in, *out;
	long dsid, metric, minsplit, minleaf, maxdepth;
	int i, verbose, force;

	argv0 = argv[0];
	in = out = 0;
	dsid = -1;
	metric = -1;
	minsplit = MINSPLIT;
	minleaf = MINLEAF;
	maxdepth = -1;
	verbose = 0;
	force = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if (!strcmp(argv[i], "-f")) {
			force = 1;
		} else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
			in = argv[++i];
		} else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
			out = argv[++i];
		} else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
			metric = toint(argv[++i], "metric");
		} else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
			dsid = toint(argv[++i], "dsid");
		} else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
			minsplit = toint(argv[++i], "minsplit");
		} else if (!strcmp(argv[i], "-l") && i + 1 < argc) {
			minleaf = toint(argv[++i], "minleaf");
		} else if (!strcmp(argv[i], "-D") && i + 1 < argc) {
			maxdepth = toint(argv[++i], "maxdepth");
		} else {
			usage();
		}
	}
	if (!in || !out || metric < 0)
		usage();
	if (metric != METRIC_GINI && metric != METRIC_ENTROPY)
		die("metric %ld is unknown; 0 is the Gini index and 1 is"
		    " entropy",
		    metric);
	if (minleaf < 1)
		die("minleaf must be at least 1");
	if (minsplit < 2 * minleaf)
		die("minsplit is %ld, but a cut leaves two parts of %ld,"
		    " so it must be at least %ld",
		    minsplit, minleaf, 2 * minleaf);
	if (!strcmp(in, out))
		die("the tree would overwrite the training database");

	memset(&d, 0, sizeof(d));
	db = dbopen(in);
	if (dsid < 0)
		dsid = dbonlydsid(db);
	dbload(db, (int)dsid, &d);
	if (sqlite3_close(db) != SQLITE_OK)
		die("cannot close %s", in);

	t = treebuild(&d, (int)metric, (int)minsplit, (int)minleaf,
	              (int)maxdepth);
	dbsavetree(out, t, force);

	if (verbose)
		fprintf(stderr,
		        "%s: dsid=%ld m=%d n=%d classes=%d metric=%s"
		        " minsplit=%ld minleaf=%ld nodes=%d leaves=%d"
		        " depth=%d\n",
		        argv0, dsid, d.m, d.n, d.nclass,
		        metric == METRIC_ENTROPY ? "entropy" : "gini", minsplit,
		        minleaf, t->num, treeleaves(t), treedepth(t));
	printf("nodes=%d\n", t->num);
	printf("leaves=%d\n", treeleaves(t));

	treefree(t);
	free(d.val);
	free(d.y);
	free(d.dpid);
	return 0;
}
