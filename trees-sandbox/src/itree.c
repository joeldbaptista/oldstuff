/*
** itree - partition a dataset by the unsupervised rule of idea n. 1.
**
** usage: itree [-v] [-q nq] [-c weighted|min] -i data.csv -o tree.csv
**
** It reads a comma-delimited matrix of points, cuts the space in two
** again and again until every point sits alone in a leaf, and writes the
** tree out as one row per node.  README.md states the idea, and tree.c
** the rule that picks each cut.
**
** The output columns are, in order: the array index of the parent, the
** array index of the left child, the array index of the right child, the
** variable the cut used, the value the cut used, the number of points in
** the node, the node's mean vector, and the spread of the left and of
** the right part.  A row's position gives the node its index, so the
** root is the first row after the header.  The root carries -1 for its
** parent, and a leaf carries -1 for its two children and its variable
** and leaves the cut and the two spreads empty.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csv.h"
#include "tree.h"
#include "util.h"

#define NQ 10

static void usage(void);
static long toint(const char *s, const char *what);

static void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [-v] [-q nq] [-c weighted|min] -i data.csv"
	        " -o tree.csv\n",
	        argv0 ? argv0 : "itree");
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
	FILE *fp;
	const char *in, *out;
	long nq;
	int i, crit, verbose, leaves;

	argv0 = argv[0];
	in = out = 0;
	nq = NQ;
	crit = CRIT_WEIGHTED;
	verbose = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
			in = argv[++i];
		} else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
			out = argv[++i];
		} else if (!strcmp(argv[i], "-q") && i + 1 < argc) {
			nq = toint(argv[++i], "nq");
		} else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
			i++;
			if (!strcmp(argv[i], "weighted"))
				crit = CRIT_WEIGHTED;
			else if (!strcmp(argv[i], "min"))
				crit = CRIT_MIN;
			else
				die("unknown criterion: %s", argv[i]);
		} else {
			usage();
		}
	}
	if (!in || !out)
		usage();
	if (nq < 2)
		die("nq must be 2 or more, so that there is a cut to try");

	csvread(&d, in);
	t = treebuild(&d, (int)nq, crit);

	if (!(fp = fopen(out, "w")))
		die("cannot write %s", out);
	treewrite(t, fp);
	if (fclose(fp) != 0)
		die("cannot write %s", out);

	if (verbose) {
		leaves = 0;
		for (i = 0; i < t->num; i++)
			if (t->v[i].leaf)
				leaves++;
		fprintf(stderr,
		        "%s: points=%d dim=%d nq=%ld criterion=%s nodes=%d"
		        " leaves=%d depth=%d\n",
		        argv0, d.n, d.dim, nq,
		        crit == CRIT_MIN ? "min" : "weighted", t->num, leaves,
		        treedepth(t));
	}
	return 0;
}
