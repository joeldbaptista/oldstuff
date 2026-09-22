-- ddl.sql - schema of a classification tree training database.
--
-- A dataset is identified by dsid, and within it dpid numbers the data
-- points and vid numbers the variables from 0 to n - 1.  This is the
-- database the sibling svm project describes, with one change: y.y
-- holds a class label 1, 2, ..., N rather than -1 or +1, because a
-- classification tree is not restricted to two classes.
--
-- A problem is a set of unlabelled data points to be dropped down a
-- tree, and it is identified by prid.  The tree itself lives in a file
-- of its own, which build-ct writes; README.md gives that schema.
--
-- The weight y.w is kept so that a dataset written for svm loads here
-- unchanged, but build-ct ignores it.  Counting is what a tree does,
-- and the node counts it stores are integers.

BEGIN;

CREATE TABLE x (
	dsid INTEGER,         /* dataset ID */
	dpid INTEGER,         /* data-point ID */
	vid INTEGER,          /* variable ID */
	val DOUBLE PRECISION  /* value of `vid` */
);

CREATE TABLE y (
	dsid INTEGER,                   /* dataset ID */
	dpid INTEGER,                   /* data-point ID */
	y INTEGER,                      /* class label: 1, 2, ..., N */
	w DOUBLE PRECISION DEFAULT 1.0  /* data point weight; ignored */
);

CREATE TABLE metadata (
	dsid INTEGER,            /* dataset ID */
	m INTEGER,               /* number of rows */
	n INTEGER,               /* number of variables */
	comment TEXT DEFAULT ''  /* just a comment for reference */
);

CREATE TABLE problem (
	prid INTEGER,         /* problem ID */
	dpid INTEGER,         /* problem data-point ID */
	vid INTEGER,          /* variable ID */
	val DOUBLE PRECISION  /* the value of the variable */
);

/* x is read both by data point, to place a row in a node, and by
   variable, to sort one variable of a node, so both orders are indexed.
   The unique indexes state what an ID means and so reject a double
   load. */
CREATE UNIQUE INDEX x_key ON x (dsid, dpid, vid);
CREATE INDEX x_dsid_vid ON x (dsid, vid);
CREATE UNIQUE INDEX y_key ON y (dsid, dpid);
CREATE UNIQUE INDEX metadata_key ON metadata (dsid);

/* A problem is read a data point at a time, to walk it down the
   tree. */
CREATE UNIQUE INDEX problem_key ON problem (prid, dpid, vid);

COMMIT;
