-- ddl.sql - schema of a logistic regression training database.
--
-- A dataset is identified by dsid, and within it dpid numbers the data
-- points and vid numbers the variables from 0 to n - 1.  A parameter set
-- is identified by pid and stands on its own, so one parameter set may
-- be used with several datasets.  Training joins the two and records a
-- model: mid names the model, cid the set of coefficients it produced,
-- and bid the intercept.
--
-- A problem is a set of unlabelled data points to be classified, and it
-- is identified by prid.  It takes prid rather than pid because pid
-- already names a parameter set.  Scoring a problem with a model writes
-- one yhat row per data point.  README.md describes the whole
-- arrangement.
--
-- Note the shape of coefficients: a logistic regression carries one
-- weight per variable, so the table is keyed by vid.  That is the one
-- structural difference from the sibling svm project, where the
-- multipliers belong to data points instead.

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
	y INTEGER,                      /* either -1 or +1 */
	w DOUBLE PRECISION DEFAULT 1.0  /* data point weight */
);

CREATE TABLE metadata (
	dsid INTEGER,            /* dataset ID */
	m INTEGER,               /* number of rows */
	n INTEGER,               /* number of variables */
	comment TEXT DEFAULT ''  /* just a comment for reference */
);

CREATE TABLE parameters (
	pid INTEGER,             /* parameter ID */
	rid INTEGER,             /* regulariser ID 0 = none 1 = L1 2 = L2 */
	lambda DOUBLE PRECISION, /* strength; ignored when rid == 0 */
	comment TEXT DEFAULT ''  /* a comment for reference */
);

CREATE TABLE coefficients (
	cid INTEGER,  /* coefficient ID */
	dsid INTEGER, /* dataset ID */
	vid INTEGER,  /* variable ID */
	beta DOUBLE PRECISION
);

CREATE TABLE bias (
	bid INTEGER,  /* bias ID */
	dsid INTEGER, /* dataset ID */
	bias DOUBLE PRECISION
);

CREATE TABLE models (
	mid INTEGER,  /* model ID */
	dsid INTEGER, /* dataset ID */
	pid INTEGER,  /* param ID */
	cid INTEGER,  /* coefficient ID */
	bid INTEGER,  /* bias ID */
	generated_at TIMESTAMP
);

CREATE TABLE problem (
	prid INTEGER,         /* problem ID */
	dpid INTEGER,         /* problem data-point ID */
	vid INTEGER,          /* variable ID */
	val DOUBLE PRECISION  /* the value of the variable */
);

CREATE TABLE yhat (
	prid INTEGER,                   /* problem ID */
	dpid INTEGER,                   /* problem data-point ID */
	mid INTEGER,                    /* model ID */
	prob_positive DOUBLE PRECISION, /* probability of positive class */
	prob_negative DOUBLE PRECISION  /* probability of negative class */
);

/* x is read both by data point, to form one inner product, and by
   variable, so both orders are indexed.  The unique indexes state what
   an ID means and so reject a double load. */
CREATE UNIQUE INDEX x_key ON x (dsid, dpid, vid);
CREATE INDEX x_dsid_vid ON x (dsid, vid);
CREATE UNIQUE INDEX y_key ON y (dsid, dpid);
CREATE UNIQUE INDEX metadata_key ON metadata (dsid);
CREATE UNIQUE INDEX parameters_key ON parameters (pid);
CREATE UNIQUE INDEX coefficients_key ON coefficients (cid, dsid, vid);
CREATE UNIQUE INDEX bias_key ON bias (bid);
CREATE UNIQUE INDEX models_key ON models (mid);
CREATE INDEX models_lookup ON models (dsid, pid);

/* A problem is read a data point at a time, to score it.  One model
   scores a data point once, which is what the unique index on yhat
   says. */
CREATE UNIQUE INDEX problem_key ON problem (prid, dpid, vid);
CREATE UNIQUE INDEX yhat_key ON yhat (prid, dpid, mid);
CREATE INDEX yhat_lookup ON yhat (prid, mid);

COMMIT;
