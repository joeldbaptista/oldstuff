/*
** Reading the training problem and writing the result back.  See db.h.
**
** Every wrapper dies on an SQLite error rather than returning one.  The
** program is a one-shot, and leaving without a COMMIT makes SQLite roll
** the transaction back, so a failed run changes nothing.
*/

#include <stdlib.h>

#include <sqlite3.h>

#include "db.h"
#include "prob.h"
#include "train.h"
#include "util.h"

static sqlite3_stmt *prep(sqlite3 *db, const char *sql);
static void run(sqlite3 *db, sqlite3_stmt *s);
static int nextid(sqlite3 *db, const char *sql);
static int rowof(const int *dpid, int m, int want);

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

static int
nextid(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *s;
	int v;

	s = prep(db, sql);
	if (sqlite3_step(s) != SQLITE_ROW)
		die("%s: %s", sql, sqlite3_errmsg(db));
	v = sqlite3_column_int(s, 0);
	sqlite3_finalize(s);
	return v;
}

/*
** Return the row that carries this data-point ID, or -1.  The IDs
** ascend, so a binary search suffices.
*/
static int
rowof(const int *dpid, int m, int want)
{
	int lo, hi, mid;

	lo = 0;
	hi = m - 1;
	while (lo <= hi) {
		mid = (lo + hi) / 2;
		if (dpid[mid] < want)
			lo = mid + 1;
		else if (dpid[mid] > want)
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
** Load dataset dsid under parameter set pid.  A data point with no row
** in x for a given vid takes the value 0.0, which is what the triple
** store means by leaving it out.
*/
void
dbload(sqlite3 *db, int dsid, int pid, Prob *p)
{
	sqlite3_stmt *s;
	double v;
	int r, vid, seen, npos, nneg;

	s = prep(db, "SELECT m, n FROM metadata WHERE dsid = ?");
	sqlite3_bind_int(s, 1, dsid);
	if (sqlite3_step(s) != SQLITE_ROW)
		die("no dataset with dsid %d", dsid);
	p->m = sqlite3_column_int(s, 0);
	p->n = sqlite3_column_int(s, 1);
	sqlite3_finalize(s);
	if (p->m < 2)
		die("dsid %d claims %d rows; at least 2 are needed", dsid,
		    p->m);
	if (p->n < 1)
		die("dsid %d claims %d variables", dsid, p->n);

	s = prep(db, "SELECT rid, lambda FROM parameters WHERE pid = ?");
	sqlite3_bind_int(s, 1, pid);
	if (sqlite3_step(s) != SQLITE_ROW)
		die("no parameter set with pid %d", pid);
	p->rid = sqlite3_column_int(s, 0);
	p->lambda = sqlite3_column_double(s, 1);
	sqlite3_finalize(s);
	if (p->rid != REG_NONE && p->rid != REG_L1 && p->rid != REG_L2)
		die("pid %d has rid %d; only 0, 1 and 2 are known", pid,
		    p->rid);
	if (p->rid != REG_NONE && !(p->lambda > 0.0))
		die("pid %d has lambda %g; a regulariser needs it positive",
		    pid, p->lambda);
	if (p->rid == REG_NONE)
		p->lambda = 0.0;

	p->val = ecalloc((size_t)p->m * p->n, sizeof(*p->val));
	p->y = ecalloc((size_t)p->m, sizeof(*p->y));
	p->w = ecalloc((size_t)p->m, sizeof(*p->w));
	p->dpid = ecalloc((size_t)p->m, sizeof(*p->dpid));

	s = prep(db, "SELECT dpid, y, w FROM y WHERE dsid = ? ORDER BY dpid");
	sqlite3_bind_int(s, 1, dsid);
	seen = 0;
	npos = 0;
	nneg = 0;
	p->wsum = 0.0;
	while (sqlite3_step(s) == SQLITE_ROW) {
		if (seen >= p->m)
			die("dsid %d holds more than the %d rows it claims",
			    dsid, p->m);
		p->dpid[seen] = sqlite3_column_int(s, 0);
		p->y[seen] = sqlite3_column_int(s, 1);
		p->w[seen] = sqlite3_column_double(s, 2);
		if (p->y[seen] != 1 && p->y[seen] != -1)
			die("dpid %d has label %d; it must be -1 or +1",
			    p->dpid[seen], p->y[seen]);
		if (!(p->w[seen] > 0.0))
			die("dpid %d has weight %g; it must be positive",
			    p->dpid[seen], p->w[seen]);
		p->wsum += p->w[seen];
		if (p->y[seen] > 0)
			npos++;
		else
			nneg++;
		seen++;
	}
	sqlite3_finalize(s);
	if (seen != p->m)
		die("dsid %d claims %d rows, but y holds %d", dsid, p->m, seen);
	if (npos == 0 || nneg == 0)
		die("dsid %d holds only one class; both are needed", dsid);

	s = prep(db, "SELECT dpid, vid, val FROM x WHERE dsid = ?");
	sqlite3_bind_int(s, 1, dsid);
	while (sqlite3_step(s) == SQLITE_ROW) {
		vid = sqlite3_column_int(s, 1);
		v = sqlite3_column_double(s, 2);
		if (vid < 0 || vid >= p->n)
			die("dsid %d has vid %d outside 0..%d", dsid, vid,
			    p->n - 1);
		if ((r = rowof(p->dpid, p->m, sqlite3_column_int(s, 0))) < 0)
			die("dsid %d has a row in x for dpid %d, which y does"
			    " not carry",
			    dsid, sqlite3_column_int(s, 0));
		p->val[(size_t)r * p->n + vid] = v;
	}
	sqlite3_finalize(s);
}

/*
** Store the fit under fresh IDs.  A logistic regression carries one
** weight per variable, so coefficients is keyed by vid.  A weight of
** exactly zero is left out, which is what makes an L1 fit compact; a
** reader takes an absent vid as zero.  The whole write sits in one
** transaction, so a failure leaves the database as it was.
*/
void
dbsave(sqlite3 *db, int dsid, const Prob *p, const Fit *f, int *cid, int *bid)
{
	sqlite3_stmt *s;
	int j;

	dbexec(db, "BEGIN IMMEDIATE");
	*cid = nextid(db, "SELECT coalesce(max(cid), 0) + 1 FROM coefficients");
	*bid = nextid(db, "SELECT coalesce(max(bid), 0) + 1 FROM bias");

	s = prep(db,
	         "INSERT INTO coefficients (cid, dsid, vid, beta)"
	         " VALUES (?, ?, ?, ?)");
	for (j = 0; j < p->n; j++) {
		if (f->beta[j] == 0.0)
			continue;
		sqlite3_bind_int(s, 1, *cid);
		sqlite3_bind_int(s, 2, dsid);
		sqlite3_bind_int(s, 3, j);
		sqlite3_bind_double(s, 4, f->beta[j]);
		run(db, s);
	}
	sqlite3_finalize(s);

	s = prep(db, "INSERT INTO bias (bid, dsid, bias) VALUES (?, ?, ?)");
	sqlite3_bind_int(s, 1, *bid);
	sqlite3_bind_int(s, 2, dsid);
	sqlite3_bind_double(s, 3, f->bias);
	run(db, s);
	sqlite3_finalize(s);

	dbexec(db, "COMMIT");
}

/*
** Load the model named by mid.  Scoring needs the weights and the
** intercept and nothing else, so the training dataset is never read.  A
** weight that was not stored is zero.
*/
void
dbloadmodel(sqlite3 *db, int mid, Model *mo)
{
	sqlite3_stmt *s;
	int vid;

	s = prep(db, "SELECT dsid, pid, cid, bid FROM models WHERE mid = ?");
	sqlite3_bind_int(s, 1, mid);
	if (sqlite3_step(s) != SQLITE_ROW)
		die("no model with mid %d", mid);
	mo->mid = mid;
	mo->dsid = sqlite3_column_int(s, 0);
	mo->pid = sqlite3_column_int(s, 1);
	mo->cid = sqlite3_column_int(s, 2);
	mo->bid = sqlite3_column_int(s, 3);
	sqlite3_finalize(s);

	s = prep(db, "SELECT n FROM metadata WHERE dsid = ?");
	sqlite3_bind_int(s, 1, mo->dsid);
	if (sqlite3_step(s) != SQLITE_ROW)
		die("model %d names dataset %d, which has no metadata", mid,
		    mo->dsid);
	mo->n = sqlite3_column_int(s, 0);
	sqlite3_finalize(s);
	if (mo->n < 1)
		die("dataset %d claims %d variables", mo->dsid, mo->n);
	mo->beta = ecalloc((size_t)mo->n, sizeof(*mo->beta));

	s = prep(db,
	         "SELECT vid, beta FROM coefficients"
	         " WHERE cid = ? AND dsid = ?");
	sqlite3_bind_int(s, 1, mo->cid);
	sqlite3_bind_int(s, 2, mo->dsid);
	while (sqlite3_step(s) == SQLITE_ROW) {
		vid = sqlite3_column_int(s, 0);
		if (vid < 0 || vid >= mo->n)
			die("cid %d names vid %d outside 0..%d", mo->cid, vid,
			    mo->n - 1);
		mo->beta[vid] = sqlite3_column_double(s, 1);
	}
	sqlite3_finalize(s);

	s = prep(db, "SELECT bias FROM bias WHERE bid = ?");
	sqlite3_bind_int(s, 1, mo->bid);
	if (sqlite3_step(s) != SQLITE_ROW)
		die("no bias with bid %d", mo->bid);
	mo->bias = sqlite3_column_double(s, 0);
	sqlite3_finalize(s);
}

/*
** Load problem prid, which must use the same n variables as the dataset
** the model was trained on.  As in x, a row that is absent means 0.0.
*/
void
dbloadproblem(sqlite3 *db, int prid, int n, Points *q)
{
	sqlite3_stmt *s;
	int i, vid, dpid, maxvid;

	q->n = n;
	s = prep(db,
	         "SELECT count(DISTINCT dpid) FROM problem"
	         " WHERE prid = ?");
	sqlite3_bind_int(s, 1, prid);
	if (sqlite3_step(s) != SQLITE_ROW)
		die("%s", sqlite3_errmsg(db));
	q->m = sqlite3_column_int(s, 0);
	sqlite3_finalize(s);
	if (q->m < 1)
		die("no problem with prid %d", prid);

	q->val = ecalloc((size_t)q->m * n, sizeof(*q->val));
	q->dpid = ecalloc((size_t)q->m, sizeof(*q->dpid));

	s = prep(db,
	         "SELECT DISTINCT dpid FROM problem WHERE prid = ?"
	         " ORDER BY dpid");
	sqlite3_bind_int(s, 1, prid);
	i = 0;
	while (sqlite3_step(s) == SQLITE_ROW)
		q->dpid[i++] = sqlite3_column_int(s, 0);
	sqlite3_finalize(s);

	s = prep(db, "SELECT dpid, vid, val FROM problem WHERE prid = ?");
	sqlite3_bind_int(s, 1, prid);
	maxvid = -1;
	while (sqlite3_step(s) == SQLITE_ROW) {
		dpid = sqlite3_column_int(s, 0);
		vid = sqlite3_column_int(s, 1);
		if (vid < 0 || vid >= n)
			die("prid %d has vid %d, but the model uses 0..%d",
			    prid, vid, n - 1);
		if (vid > maxvid)
			maxvid = vid;
		if ((i = rowof(q->dpid, q->m, dpid)) < 0)
			die("prid %d has an unknown dpid %d", prid, dpid);
		q->val[(size_t)i * n + vid] = sqlite3_column_double(s, 2);
	}
	sqlite3_finalize(s);

	/* A problem that never mentions the last variable is almost
	** always one built for a different dataset.  Scoring it would
	** quietly treat the missing variables as zero and return
	** nonsense, so refuse instead. */
	if (maxvid != n - 1)
		die("prid %d reaches vid %d, but the model uses 0..%d;"
		    " the problem was built for a different dataset",
		    prid, maxvid, n - 1);
}

/*
** Write one yhat row per data point.  With replace set, any earlier
** scoring of this problem by this model is dropped first.
*/
void
dbsaveyhat(sqlite3 *db, int prid, int mid, const Points *q, const double *pos,
           int replace)
{
	sqlite3_stmt *s;
	int i;

	dbexec(db, "BEGIN IMMEDIATE");
	if (replace) {
		s = prep(db, "DELETE FROM yhat WHERE prid = ? AND mid = ?");
		sqlite3_bind_int(s, 1, prid);
		sqlite3_bind_int(s, 2, mid);
		run(db, s);
		sqlite3_finalize(s);
	}
	s = prep(db,
	         "INSERT INTO yhat (prid, dpid, mid, prob_positive,"
	         " prob_negative) VALUES (?, ?, ?, ?, ?)");
	for (i = 0; i < q->m; i++) {
		sqlite3_bind_int(s, 1, prid);
		sqlite3_bind_int(s, 2, q->dpid[i]);
		sqlite3_bind_int(s, 3, mid);
		sqlite3_bind_double(s, 4, pos[i]);
		sqlite3_bind_double(s, 5, 1.0 - pos[i]);
		run(db, s);
	}
	sqlite3_finalize(s);
	dbexec(db, "COMMIT");
}
