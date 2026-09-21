# Logistic regression

## Introduction

This repo implements the weighted, regularised binary logistic regression. It
does two things: it fits a model from labelled data, and it classifies
unlabelled data with a model it fitted earlier.

Every data point carries its own weight, and that weight scales the penalty the
point may incur. So a point with a large weight is expensive to misclassify,
and a point with a small weight is cheap. The fit may carry an L1 or an L2
penalty on the weights, or none at all.

Everything lives in one SQLite3 database. The shell scripts fill it, two C99
programs work on it, and every result goes back into the same file. No model
and no classification is ever written outside the database.

The two programs are `logreg`, which fits, and `predict`, which classifies. The
shell scripts drive them and keep the bookkeeping, so nothing but those two
needs to do arithmetic.

The pipeline runs in six steps:

```
make-training.sh       an empty database
add-training-data.sh   labelled data        -> dsid
add-parameters.sh      penalty and lambda   -> pid
train-logreg.sh        dsid + pid           -> mid    (runs logreg)
add-problem.sh         unlabelled data      -> prid
apply-logreg.sh        prid + mid           -> yhat   (runs predict)
```

Fitting writes `coefficients` and `bias`, and records the pair in `models`.
Classifying writes `yhat`, one row per data point, holding the probability of
each class.

This is the sibling of the `svm` project next door, and deliberately so: the
same database, the same file formats, the same six steps. Two things differ,
and this list is complete. First, `coefficients` is keyed by `vid` rather than
by `dpid`, because a logistic regression carries one weight per variable
whereas a support vector machine carries one multiplier per data point. Second,
there is no Platt scaling, because a logistic regression returns a probability
of its own.

## The database

```SQL
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
```

A dataset is identified by `dsid`. Within a dataset, `dpid` numbers the data
points and `vid` numbers the variables from 0 to `n - 1`. A row that is absent
from `x` means the value 0.0, so a sparse dataset costs only the rows it
actually needs.

A parameter set is identified by `pid` and stands on its own, so the same `pid`
may be fitted against several datasets. `rid` names the penalty: 0 for none, 1
for L1, 2 for L2. `lambda` is ignored when `rid` is 0.

Fitting joins a dataset to a parameter set and records one row in `models`.
That row points at the weights it produced, under `cid`, and at the intercept,
under `bid`. Fitting is additive: every run takes fresh IDs, so earlier models
stay where they were.

A problem is a set of unlabelled data points to be classified, and it is
identified by `prid`. Its rows have the same shape as those of `x`. Scoring a
problem with a model writes one `yhat` row per data point. A problem takes
`prid` rather than `pid` because `pid` already names a parameter set.

A weight of exactly zero is left out of `coefficients`, and a reader takes an
absent `vid` as zero. That is what makes an L1 fit compact: at lambda 0.02 on
the sonar data only 13 of the 60 weights survive, so only 13 rows are written.

`sql/ddl.sql` adds three things the listing above does not show, and this list
is complete: a default of 1.0 on `y.w`, a default of the empty string on both
`comment` columns, and indexes. The indexes on `x (dsid, dpid, vid)`,
`y (dsid, dpid)`, `metadata (dsid)`, `parameters (pid)`,
`coefficients (cid, dsid, vid)`, `bias (bid)`, `models (mid)`,
`problem (prid, dpid, vid)` and `yhat (prid, dpid, mid)` are unique, so loading
the same thing twice is rejected rather than silently doubled.

## Input files

All four input files are delimited by `|`, not by a comma.

The data file holds one data point per row and `n + 2` fields per row, with no
header. Field 1 is the label, which is -1 or +1. Field 2 is the weight, which
must be positive. Fields 3 onwards are the variables, filed under `vid` 0 to
`n - 1`. Data points are numbered from 0 in the order they are read.

```
1|1.0|-0.727139|-0.687098|...
-1|1.0|-0.297935|-0.765967|...
```

The metadata file holds a header row and one data row. Its columns are `m`, the
number of rows, `n`, the number of variables, and `comment`, which may also be
spelled `comments`.

```
m|n|comments
156|60|sonar, training part
```

The parameters file holds a header row and one row per parameter set. Its
columns are `reg`, which may also be spelled `rid`, `lambda`, and `comment`.

```
reg|lambda|comment
2|0.01|L2, lambda 0.01
1|0.02|L1, lambda 0.02
```

The problem file holds one data point per row and no header, with no label and
no weight. Every field is the value of one variable, filed under `vid` 0
onwards, and the count must equal the `n` of the dataset the scoring model was
fitted on.

```
-0.727139|-0.687098|-0.728647|...
```

## Building

```
$ make          # compiles src/*.o
$ make bin      # links ./logreg and ./predict, which the scripts call
```

`make logreg` and `make predict` link one program each. `make fmt` runs
`clang-format` over the sources using the `.clang-format` in this directory,
and `make clean` removes the binaries and the objects. There is no install
target, because the tools are meant to be run from here.

The only dependency is SQLite3.

## How-to

Create an empty training database:

```
$ ./make-training.sh -o training.sqlite3
```

Pass `-f` to overwrite a database that already exists.

Populate it, add a parameter set, and fit:

```
$ ./add-training-data.sh -d data/sonar-train.csv \
      -m data/sonar-train-metadata.csv -t training.sqlite3
dsid=1
$ ./add-parameters.sh -m data/params.csv -t training.sqlite3
pid=1
pid=2
pid=3
pid=4
pid=5
$ ./train-logreg.sh -t training.sqlite3 -d 1 -p 3
mid=1
```

Then load points to classify, and classify them:

```
$ ./add-problem.sh -i data/sonar-test.csv -t training.sqlite3
prid=1
$ ./apply-logreg.sh -p 1 -m 1 -t training.sqlite3
51
```

Add `-v` to either driver for a line on stderr. From `train-logreg.sh` it
reports the iterations used, the objective, the loss alone, the intercept and
how many weights are not zero. A model scores a problem once, so re-scoring
needs `-f`.

Nothing is written when a step fails. Each tool checks its whole input before
it touches the database, and the writes sit inside one transaction.

## A worked test

`data/split-sonar.sh` cuts `data/sonar.csv` into a training part of 156 rows
and a test part of 51, stratified so the class balance holds, and writes
`sonar-test-labels.csv` alongside so the result can be checked.

Fitting all five settings in `data/params.csv` and scoring the held-out part
gives:

```
mid  comment           tp  tn  fp  fn  accuracy  nnz
---  ----------------  --  --  --  --  --------  ---
1    L2, lambda 0.001  19  23  4   5   82.35     60
2    L2, lambda 0.01   19  24  3   5   84.31     60
3    L2, lambda 0.1    19  25  2   5   86.27     60
4    L1, lambda 0.005  17  23  4   7   78.43     30
5    L1, lambda 0.02   18  23  4   6   80.39     13
```

`nnz` counts the weights that survived. The L2 rows keep all 60 and improve as
lambda rises, which is the penalty holding back the overfitting that 60
variables over 156 points invites. The L1 rows drop most of the variables, 30
and then 13 of 60, at a few points of accuracy. That is the trade the two
penalties offer, and it is the reason to have both.

The query behind that table, with the true labels loaded into a temporary
table since the schema keeps none:

```sh
$ sqlite3 training.sqlite3 <<'SQL'
CREATE TEMP TABLE truth (dpid INTEGER, y INTEGER);
.mode csv
.separator |
.import --skip 1 data/sonar-test-labels.csv truth
.mode column
.headers on
SELECT h.mid, p.comment,
       sum(pred =  1 AND t.y =  1) AS tp, sum(pred = -1 AND t.y = -1) AS tn,
       sum(pred =  1 AND t.y = -1) AS fp, sum(pred = -1 AND t.y =  1) AS fn,
       round(100.0 * sum(pred = t.y) / count(*), 2) AS accuracy,
       (SELECT count(*) FROM coefficients c WHERE c.cid = m.cid) AS nnz
FROM (SELECT mid, prid, dpid,
             CASE WHEN prob_positive > 0.5 THEN 1 ELSE -1 END AS pred
      FROM yhat) h
JOIN truth t      ON t.dpid = h.dpid
JOIN models m     ON m.mid = h.mid
JOIN parameters p ON p.pid = m.pid
WHERE h.prid = 1
GROUP BY h.mid, p.comment, m.cid ORDER BY h.mid;
SQL
```

## The model

Reading a model back means joining `models` to `coefficients` and `bias`:

```SQL
SELECT c.vid, c.beta, b.bias
FROM models m
JOIN coefficients c ON c.cid = m.cid AND c.dsid = m.dsid
JOIN bias b         ON b.bid = m.bid
WHERE m.mid = ?
ORDER BY c.vid;
```

The fitted probability is

```
P(y = +1 | x) = 1 / (1 + exp(-(x . beta + b)))
```

and `yhat.prob_positive` holds it. The two probabilities sum to 1, and the
predicted label is +1 when `prob_positive` exceeds 0.5, which here is exactly
`x . beta + b > 0`. There is no separate threshold to worry about, unlike the
svm project where the sigmoid is fitted afterwards and sits at `-B/A`.

`predict` prints the linear score itself under `-n`, which writes nothing:

```
$ ./predict -n -t training.sqlite3 -p 1 -m 3 | head -2
```

The three fields are the `dpid`, `x . beta + b`, and `prob_positive`.

## Method

The fit minimises

```
J(beta, b) = (1/W) sum_i w_i log(1 + exp(-y_i (x_i . beta + b)))
             + lambda R(beta)
```

where `W` is the sum of the weights. Averaging by `W` rather than summing keeps
lambda on a scale that does not move with the size of the dataset or with how
the weights happen to be scaled. `R` is `(1/2)||beta||^2` for L2, `||beta||_1`
for L1, and nothing at all when `rid` is 0.

The intercept is never penalised. Penalising it would tie the fit to wherever
the origin happens to sit, which is not a property of the data.

The method is gradient descent. The step is `1/L`, where `L` bounds the
curvature of the smooth part: the Hessian of the averaged loss is

```
(1/W) sum_i w_i p_i (1 - p_i) [x_i; 1] [x_i; 1]'
```

and `p(1-p)` never exceeds 1/4, so its largest eigenvalue is at most the trace,
`(1/(4W)) sum_i w_i (||x_i||^2 + 1)`. For L2 the penalty adds lambda. That step
needs no tuning and cannot overshoot.

### The L1 tweak

`|beta_j|` has no derivative at zero, so it cannot go into the gradient. The
tweak is to leave the penalty out of the gradient and apply it afterwards, by
the proximal step of the L1 norm, which is the soft threshold

```
beta_j <- sign(u_j) max(|u_j| - eta lambda, 0)
```

on `u = beta - eta grad(loss)`. That is proximal gradient descent, and it is
the standard answer. It has two virtues over simply feeding the subgradient
`sign(beta_j)` to the same loop: it sets weights to exactly zero rather than
leaving them to drift near zero, which is the whole point of an L1 penalty; and
it keeps the convergence rate of plain gradient descent instead of the slower
one a subgradient method settles for.

### Stopping

The loop stops when the largest change in any weight or in the intercept falls
below `tol` times the largest of them, with `tol` defaulting to 1e-9 and
settable with `-e`. It gives up after `-k` iterations, 200000 by default.

With `rid` 0 and data that a hyperplane can separate there is no finite
optimum: the weights grow without bound and the loss approaches zero. `logreg`
does not converge in that case, correctly, and says so.

## Validation

The fit was checked against `liblinear-weights-2.49`, the instance-weighted
LIBLINEAR by Chang, Lin, Tsai, Ho and Yu. Their objective is
`R(beta) + C sum_i w_i loss_i`, so dividing ours by lambda matches it at
`C = 1 / (lambda W)`. Comparing with the intercept held at zero on both sides,
using `-B` here and liblinear's default:

```
                          uniform weights    weights 0.1 to 4.0
L2, lambda 0.01              2.9e-07              1.2e-08
L2, lambda 0.1               3.8e-07              1.2e-09
L1, lambda 0.01              7.9e-07              3.9e-09
```

Those are the largest differences in any weight, relative to the largest
weight. For the L1 rows the two agree not only in value but in which weights
survive: 21 of 60 on the uniform run and 20 of 60 on the weighted one, the same
ones on both sides. That is the part worth checking, because recovering the
right support is what an L1 solver is for.

A second check uses no external code. At the solution the optimality conditions
must hold: for L2, `grad + lambda beta = 0` in every coordinate; for L1,
`grad + lambda sign(beta_j) = 0` where the weight is not zero and
`|grad| <= lambda` where it is; and the intercept's own gradient must vanish,
since it carries no penalty. Evaluated from an independent implementation of
the objective, the largest violation is:

```
L2, lambda 0.01     2.1e-11
L2, lambda 0.1      9.8e-12
L1, lambda 0.01     1.5e-11
L1, lambda 0.05     5.7e-12
```

Both programs are clean under UBSan with `-fno-sanitize-recover=all` and the
system malloc guards, with output identical to the plain build. There are no
compiler warnings under `-std=c99 -pedantic -Wall -Wextra`, `clang-format` is
idempotent over the sources, and no line runs past 80 columns.

## Layout

```
make-training.sh      creates an empty database from sql/ddl.sql
add-training-data.sh  loads one dataset
add-parameters.sh     loads parameter sets
train-logreg.sh       runs logreg, then records the model
add-problem.sh        loads data points to be classified
apply-logreg.sh       runs predict, which writes yhat
sql/ddl.sql           the schema
src/                  the C99 solver and scorer
data/                 example inputs, and split-sonar.sh
```

Inside `src`:

```
logreg.c    the logreg command line, which fits
predict.c   the predict command line, which classifies
db.c        reads datasets, models and problems; writes the results
train.c     the objective, the gradient, and the two descent rules
prob.h      the problem, the points and the model, as held in memory
util.c      die and allocation
```

The C is written in the suckless style that `.clang-format` encodes, the same
one the sibling `dataset`, `svm` and `trees-sandbox` projects use.
