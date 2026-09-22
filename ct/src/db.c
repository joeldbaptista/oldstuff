/*
** Reading the training set and writing the tree back.  See db.h.
**
** Every wrapper dies on an SQLite error rather than returning one.  The
** program is a one-shot, and leaving without a COMMIT makes SQLite roll
** the transaction back, so a failed run changes nothing.
*/

#include <stdio.h>
#include <stdlib.h>

#include <sqlite3.h>

#include "data.h"
#include "db.h"
#include "tree.h"
#include "util.h"

/* The schema of the file build-ct writes.  README.md describes it. */
static const char treeddl[] =
        "BEGIN;"
        "CREATE TABLE ct ("
        "	nidx INTEGER,"   /* node index */
        "	lidx INTEGER,"   /* index of the left child */
        "	ridx INTEGER,"   /* index of the right child */
        "	pidx INTEGER,"   /* the index of the parent; NULL at root */
        "	cutvar INTEGER," /* cut variable */
        "	cutval DOUBLE PRECISION," /* cut value */
        "	size INTEGER,"            /* data points in the node */
        "	majority INTEGER,"        /* most frequent class */
        "	level INTEGER"            /* level; 0 at the root */
        ");"
        "CREATE TABLE node_distribution ("
        "	nidx INTEGER,"     /* node ID */
        "	class_id INTEGER," /* class ID: 1, 2, ..., N */
        "	count INTEGER"     /* elements of class_id in nidx */
        ");"
        "CREATE UNIQUE INDEX ct_key ON ct (nidx);"
        "CREATE INDEX ct_parent ON ct (pidx);"
        "CREATE UNIQUE INDEX node_distribution_key"
        " ON node_distribution (nidx, class_id);"
        "COMMIT;";

static sqlite3_stmt *prep(sqlite3 *db, const char *sql);
static void run(sqlite3 *db, sqlite3_stmt *s);
static int rowof(const Data *d, int dpid);

static sqlite3_stmt *
prep(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *s;

	if (sqlite3_prepare_v2(db, sql, -1, &s, 0) != SQLITE_OK)
		die("%s: %s", sql, sqlite3_errmsg(db));
	return s;
}

static void
run(sqlite3 *db, sqlite3_stmt *s)
{
	if (sqlite3_step(s) != SQLITE_DONE)
		die("%s", sqlite3_errmsg(db));
	sqlite3_reset(s);
	sqlite3_clear_bindings(s);
}

/*
** Return the row that carries this dpid, or -1.  The IDs ascend, so a
** binary search suffices and no separate map is needed.
*/
static int
rowof(const Data *d, int dpid)
{
	int lo, hi, mid;

	lo = 0;
	hi = d->m - 1;
	while (lo <= hi) {
		mid = (lo + hi) / 2;
		if (d->dpid[mid] < dpid)
			lo = mid + 1;
		else if (d->dpid[mid] > dpid)
			hi = mid - 1;
		else
			return mid;
	}
	return -1;
}

sqlite3 *
dbopen(const char *path)
{
	sqlite3 *db;

	if (sqlite3_open(path, &db) != SQLITE_OK)
		die("cannot open %s: %s", path, sqlite3_errmsg(db));
	return db;
}

void
dbexec(sqlite3 *db, const char *sql)
{
	char *err;

	err = 0;
	if (sqlite3_exec(db, sql, 0, 0, &err) != SQLITE_OK)
		die("%s", err ? err : sqlite3_errmsg(db));
}

/*
** The dsid of the one dataset in the file.  A file holding several is
** an error here, because the command line names no dataset; -d picks
** one in that case.
*/
int
dbonlydsid(sqlite3 *db)
{
	sqlite3_stmt *s;
	int k, dsid;

	s = prep(db, "SELECT count(*), coalesce(min(dsid), 0) FROM metadata");
	if (sqlite3_step(s) != SQLITE_ROW)
		die("%s", sqlite3_errmsg(db));
	k = sqlite3_column_int(s, 0);
	dsid = sqlite3_column_int(s, 1);
	sqlite3_finalize(s);
	if (k == 0)
		die("the training database holds no dataset");
	if (k > 1)
		die("the training database holds %d datasets; name one"
		    " with -d",
		    k);
	return dsid;
}

/*
** Load dataset dsid.  A data point with no row in x for a given vid
** takes the value 0.0, which is what the triple store means by leaving
** it out.
*/
void
dbload(sqlite3 *db, int dsid, Data *d)
{
	sqlite3_stmt *s;
	int *seen;
	double v;
	int i, r, vid, lab, got;

	s = prep(db, "SELECT m, n FROM metadata WHERE dsid = ?");
	sqlite3_bind_int(s, 1, dsid);
	if (sqlite3_step(s) != SQLITE_ROW)
		die("no dataset with dsid %d", dsid);
	d->m = sqlite3_column_int(s, 0);
	d->n = sqlite3_column_int(s, 1);
	sqlite3_finalize(s);
	if (d->m < 2)
		die("dsid %d claims %d rows; at least 2 are needed", dsid,
		    d->m);
	if (d->n < 1)
		die("dsid %d claims %d variables", dsid, d->n);

	d->val = ecalloc((size_t)d->m * d->n, sizeof(*d->val));
	d->y = ecalloc((size_t)d->m, sizeof(*d->y));
	d->dpid = ecalloc((size_t)d->m, sizeof(*d->dpid));

	s = prep(db, "SELECT dpid, y FROM y WHERE dsid = ? ORDER BY dpid");
	sqlite3_bind_int(s, 1, dsid);
	got = 0;
	d->nclass = 0;
	while (sqlite3_step(s) == SQLITE_ROW) {
		if (got >= d->m)
			die("dsid %d holds more than the %d rows it claims",
			    dsid, d->m);
		d->dpid[got] = sqlite3_column_int(s, 0);
		lab = sqlite3_column_int(s, 1);
		if (lab < 1)
			die("dpid %d has label %d; labels run 1, 2, ..., N",
			    d->dpid[got], lab);
		d->y[got] = lab;
		if (lab > d->nclass)
			d->nclass = lab;
		got++;
	}
	sqlite3_finalize(s);
	if (got != d->m)
		die("dsid %d claims %d rows, but y holds %d", dsid, d->m, got);

	/* The labels are 1 to N, so a gap in that range means the
	** dataset was labelled some other way, and the empty class
	** would sit in every node distribution saying nothing. */
	seen = ecalloc((size_t)d->nclass + 1, sizeof(*seen));
	for (i = 0; i < d->m; i++)
		seen[d->y[i]] = 1;
	for (i = 1; i <= d->nclass; i++)
		if (!seen[i])
			die("dsid %d reaches class %d, but carries no row of"
			    " class %d; labels must run 1, 2, ..., N",
			    dsid, d->nclass, i);
	free(seen);
	if (d->nclass < 2)
		die("dsid %d holds only class 1; at least 2 are needed", dsid);

	s = prep(db, "SELECT dpid, vid, val FROM x WHERE dsid = ?");
	sqlite3_bind_int(s, 1, dsid);
	while (sqlite3_step(s) == SQLITE_ROW) {
		vid = sqlite3_column_int(s, 1);
		v = sqlite3_column_double(s, 2);
		if (vid < 0 || vid >= d->n)
			die("dsid %d has vid %d outside 0..%d", dsid, vid,
			    d->n - 1);
		if ((r = rowof(d, sqlite3_column_int(s, 0))) < 0)
			die("dsid %d has a row in x for dpid %d, which y does"
			    " not carry",
			    dsid, sqlite3_column_int(s, 0));
		d->val[(size_t)r * d->n + vid] = v;
	}
	sqlite3_finalize(s);
}

/*
** Write the tree into a file of its own.  Nodes are numbered from 1 in
** the order they were built, so the root is node 1 and a child always
** carries a higher number than its parent.  A leaf stores no children
** and no cut, and the root stores no parent, so those columns are NULL
** there.  The level column is the depth the builder recorded, counting
** the root as 0.  Only the classes a node actually carries get a
** distribution row, because a count of zero says nothing.
**
** The whole write sits in one transaction, so a failure leaves a file
** with a schema and no rows rather than half a tree.
*/
void
dbsavetree(const char *path, const Tree *t, int force)
{
	sqlite3 *db;
	sqlite3_stmt *s;
	const Node *n;
	FILE *fp;
	int i, k;

	if ((fp = fopen(path, "r"))) {
		fclose(fp);
		if (!force)
			die("%s exists; pass -f to overwrite it", path);
		if (remove(path) != 0)
			die("cannot remove %s", path);
	}

	db = dbopen(path);
	dbexec(db, treeddl);
	dbexec(db, "BEGIN IMMEDIATE");

	s = prep(db,
	         "INSERT INTO ct (nidx, lidx, ridx, pidx, cutvar, cutval,"
	         " size, majority, level)"
	         " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
	for (i = 0; i < t->num; i++) {
		n = &t->v[i];
		sqlite3_bind_int(s, 1, i + 1);
		if (n->left < 0) {
			sqlite3_bind_null(s, 2);
			sqlite3_bind_null(s, 3);
			sqlite3_bind_null(s, 5);
			sqlite3_bind_null(s, 6);
		} else {
			sqlite3_bind_int(s, 2, n->left + 1);
			sqlite3_bind_int(s, 3, n->right + 1);
			sqlite3_bind_int(s, 5, n->cutvar);
			sqlite3_bind_double(s, 6, n->cutval);
		}
		if (n->parent < 0)
			sqlite3_bind_null(s, 4);
		else
			sqlite3_bind_int(s, 4, n->parent + 1);
		sqlite3_bind_int(s, 7, n->size);
		sqlite3_bind_int(s, 8, n->majority);
		sqlite3_bind_int(s, 9, n->depth);
		run(db, s);
	}
	sqlite3_finalize(s);

	s = prep(db,
	         "INSERT INTO node_distribution (nidx, class_id, count)"
	         " VALUES (?, ?, ?)");
	for (i = 0; i < t->num; i++) {
		n = &t->v[i];
		for (k = 1; k <= t->nclass; k++) {
			if (n->count[k] == 0)
				continue;
			sqlite3_bind_int(s, 1, i + 1);
			sqlite3_bind_int(s, 2, k);
			sqlite3_bind_int(s, 3, n->count[k]);
			run(db, s);
		}
	}
	sqlite3_finalize(s);

	dbexec(db, "COMMIT");
	if (sqlite3_close(db) != SQLITE_OK)
		die("cannot close %s", path);
}
