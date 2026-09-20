# SVM

## Introduction

This repo implements the weighted binary-classifier SVM. It does two things:
it trains a model from labelled data, and it classifies unlabelled data with a
model it trained earlier.

Every data point carries its own weight, and that weight scales the penalty the
point may incur. So a point with a large weight is expensive to misclassify,
and a point with a small weight is cheap.

Everything lives in one SQLite3 database. The shell scripts fill it, two C99
programs work on it, and every result goes back into the same file. No model
and no classification is ever written outside the database.

The two programs are `smo`, which trains, and `score`, which classifies. The
shell scripts drive them and keep the bookkeeping, so nothing but those two
needs to do arithmetic.

The pipeline runs in six steps:

```
make-training.sh       an empty database
add-training-data.sh   labelled data      -> dsid
add-parameters.sh      kernel and cost    -> pid
train-svm.sh           dsid + pid         -> mid    (runs smo)
add-problem.sh         unlabelled data    -> prid
apply-svm.sh           prid + mid         -> yhat   (runs score)
```

Training writes `coefficients` and `bias`, and records the pair in `models`.
Classifying writes `yhat`, one row per data point, holding the probability of
each class.

## The database

```SQL
CREATE TABLE x (
	dsid INTEGER, /* dataset ID */
	dpid INTEGER, /* data-point ID */
	vid INTEGER, /* variable ID */
	val DOUBLE PRECISION /* value of `vid` */
);

CREATE TABLE y (
	dsid INTEGER, /* dataset ID */
	dpid INTEGER, /* data-point ID */
	y INTEGER, /* either -1 or +1 */
	w DOUBLE PRECISION /* data point weight; default 1.0 */
);

CREATE TABLE metadata (
	dsid INTEGER, /* dataset ID */
	m INTEGER, /* number of rows */
	n INTEGER, /* number of variables */
	comment TEXT /* just a comment for reference */	
);

CREATE TABLE parameters (
	pid INTEGER, /* parameter ID */
	kid INTEGER, /* kernel ID 0 = Linear 1 = Radial */
	cost DOUBLE PRECISION,
	gamma DOUBLE PRECISION, /* if kid == 0, gamma is ignored */
	comment TEXT /* A comment for reference; default "" */
);

CREATE TABLE coefficients (
	cid INTEGER, /* coefficient ID */
	dsid INTEGER, /* dataset ID */
	dpid INTEGER, /* data-point ID */
	alpha DOUBLE PRECISION
);

CREATE TABLE bias (
	bid INTEGER, /* Bias ID */
	dsid INTEGER, /* dataset ID */
	bias DOUBLE PRECISION
);

CREATE TABLE models (
	mid INTEGER, /* model ID */
	dsid INTEGER, /* dataset ID */
	pid INTEGER, /* param ID */
	cid INTEGER, /* coefficient ID */
	bid INTEGER, /* bias ID*/
	generated_at TIMESTAMP
);

CREATE TABLE problem (
	prid INTEGER, /* problem ID */
	dpid INTEGER, /* problem data-point ID */
	vid INTEGER, /* variable ID */
	val DOUBLE PRECISION /* the value of the variable */
);

CREATE TABLE yhat (
	prid INTEGER, /* problem ID */
	dpid INTEGER, /* problem data-point ID */
	mid INTEGER, /* model ID */
	prob_positive DOUBLE PRECISION, /* probability of positive class */
	prob_negative DOUBLE PRECISION /* probability of negative class */
);
```

A dataset is identified by `dsid`. Within a dataset, `dpid` numbers the data
points and `vid` numbers the variables from 0 to `n - 1`. A row that is absent
from `x` means the value 0.0, so a sparse dataset costs only the rows it
actually needs.

A parameter set is identified by `pid` and stands on its own, so the same `pid`
may be trained against several datasets.

Training joins a dataset to a parameter set and records one row in `models`.
That row points at the multipliers it produced, under `cid`, and at the
threshold, under `bid`. Training is additive: every run takes fresh IDs, so
earlier models stay where they were.

A problem is a set of unlabelled data points to be classified, and it is
identified by `prid`. Its rows have the same shape as those of `x`, so an
absent row means the value 0.0, and `vid` must match the variables of the
dataset the model was trained on. Scoring a problem with a model writes one
`yhat` row per data point, naming the problem, the point and the model that
produced it.

A problem takes `prid` rather than `pid` because `pid` already names a
parameter set, in `parameters` and in `models`. Two ID spaces sharing one
column name would let a careless join match the wrong rows silently.

`sql/ddl.sql` adds three things the listing above does not show, and this list
is complete: a default of 1.0 on `y.w`, a default of the empty string on both
`comment` columns, and indexes. The indexes on `x (dsid, dpid, vid)`,
`y (dsid, dpid)`, `metadata (dsid)`, `parameters (pid)`,
`coefficients (cid, dsid, dpid)`, `bias (bid)`, `models (mid)`,
`problem (prid, dpid, vid)` and `yhat (prid, dpid, mid)` are unique, so loading
the same thing twice is rejected rather than silently doubled. The last of
those also states that one model scores a data point once.

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
207|60|this is just a test
```

The problem file holds one data point per row and no header. Every field is
the value of one variable, filed under `vid` 0 onwards. There is no label and
no weight, which is what separates a problem from a training set. Every row
must carry the same number of fields, and that number must equal the `n` of
the dataset the scoring model was trained on. `data/new-sonar.csv` is an
example, being `data/sonar.csv` with its label and weight dropped.

```
-0.727139|-0.687098|-0.728647|...
-0.297935|-0.765967|-0.620894|...
```

The parameters file holds a header row and one row per parameter set. Its
columns are `kernel`, which may also be spelled `kid`, `cost`, `gamma` and
`comment`. Gamma is ignored when the kernel is linear, but it is still stored.

```
kernel|cost|gamma|comment
1|100|1.2|this is just a test
0|100|1.2|this is another a test
```

## Building

```
$ make          # compiles src/*.o
$ make bin      # links ./smo and ./score, which the scripts call
```

`make smo` and `make score` link one program each.

`make fmt` runs `clang-format` over the sources, using the `.clang-format` in
this directory. `make clean` removes the binary and the objects. There is no
install target, because the tools are meant to be run from here.

The only dependency is SQLite3.

## How-to

Create an empty training database:

```
$ ./make-training.sh -o training.sqlite3
```

Pass `-f` to overwrite a database that already exists. Without it, an existing
file is an error.

Populate the training database like this:

```
$ ./add-training-data.sh -d data/sonar.csv -m data/sonar-metadata.csv -t training.sqlite3
dsid=1
```

Add a set of parameters:

```
$ ./add-parameters.sh -m data/params.csv -t training.sqlite3
pid=1
pid=2
```

This returns one `pid` per row of the file. Create the model like this:

```
$ ./train-svm.sh -t training.sqlite3 -d <dsid> -p <pid>
mid=1
```

and this returns the model ID. Add `-v` for a line on stderr reporting the
iterations used, the objective, the threshold and the number of support
vectors.

Load a set of data points to be classified:

```
$ ./add-problem.sh -i data/new-sonar.csv -t training.sqlite3
prid=1
```

Classify them with a model:

```
$ ./apply-svm.sh -p <prid> -m <mid> -t training.sqlite3
207
```

and this returns the number of data points classified. A model scores a
problem once, so re-scoring needs `-f`, which drops the earlier rows first.
Add `-v` for a line on stderr naming the model, the sizes and the two fitted
sigmoid parameters.

Nothing is written when a step fails. Each tool checks its whole input before
it touches the database, and the writes sit inside one transaction.

## The model

Reading a model back means joining `models` to `coefficients` and `bias`:

```SQL
SELECT c.dpid, c.alpha, b.bias
FROM models m
JOIN coefficients c ON c.cid = m.cid AND c.dsid = m.dsid
JOIN bias b        ON b.bid = m.bid
WHERE m.mid = ?;
```

The decision function is

```
f(x) = sum_i y_i alpha_i K(x_i, x) + b
```

where the sum runs over the stored `dpid` values and `y_i` comes from table `y`.
The predicted label is the sign of `f(x)`.

Four conventions matter when reading the tables, and this list is complete:

- `alpha` holds the multiplier itself, which is never negative. The label is
  not folded into it, so prediction multiplies by `y_i`.
- `bias` holds `b` as it appears above. It is the negation of the quantity
  LIBSVM calls `rho`.
- Only support vectors are stored, those with a multiplier above zero. A
  multiplier of zero says nothing about the model, so it is left out.
- The kernel is `K(u,v) = u . v` when `kid` is 0, and
  `K(u,v) = exp(-gamma ||u - v||^2)` when `kid` is 1.

## Classification

`apply-svm.sh` evaluates `f(x)` at every data point of a problem and writes one
`yhat` row for each. Reading the result back means joining on the problem and
the model:

```SQL
SELECT h.dpid, h.prob_positive, h.prob_negative
FROM yhat h
WHERE h.prid = ? AND h.mid = ?
ORDER BY h.dpid;
```

`prob_positive` is the probability that the class is +1, and the two
probabilities sum to 1.

The decision function returns a signed real, not a probability, so a sigmoid

```
P(y = +1 | x) = 1 / (1 + exp(A f(x) + B))
```

is fitted to it by Platt scaling, in the form given by Lin, Weng and Lin.
Fitting on the training decision values alone would be circular, because the
model already separates those points better than it will separate new ones. So
the fit uses cross-validated values instead: the training data is cut into five
parts, each part is scored by a model trained on the other four, and `A` and
`B` are fitted to the pooled result.

Three consequences are worth knowing, and this list is complete:

- Scoring costs six trainings rather than one, five for the folds and the
  one already recorded. The sigmoid is fitted at scoring time because the
  schema keeps no room for `A` and `B`.
- The folds are deterministic. Points are dealt to folds in turn within each
  class, which keeps the class balance of every fold close to that of the whole
  and makes a run reproducible. LIBSVM shuffles at random instead, so its `A`
  and `B` move with the seed.
- `prob_positive > 0.5` is not exactly `f(x) > 0`. The threshold sits at
  `-B/A`, so the two can disagree on a point that falls between them. The same
  is true of LIBSVM.

A problem must mention its last variable, `vid = n - 1`, somewhere. Absent rows
mean 0.0, as in `x`, but a problem that never reaches `n - 1` is almost always
one built for a different dataset, and scoring it would quietly treat the
missing variables as zero. That is refused rather than scored.

## Method

`smo` solves the dual of the weighted C-SVC,

```
min_a   1/2 a' Q a - e' a
s.t.    y' a = 0
        0 <= a_i <= c_i,   c_i = w_i * cost
```

where `Q_ij = y_i y_j K(x_i, x_j)`. Instance weights enter in exactly one
place, the upper bound `c_i`. That is the whole of the weighted extension, and
it is why the rest of the method is the ordinary C-SVC one.

The solver is SMO, with a working set of two variables. Two is the smallest set
that can change while `y' a = 0` still holds, so no smaller decomposition is
available. And a two-variable sub-problem has a closed form, which means the
program needs no QP library and no linear algebra beyond a dot product.

Three further points describe the implementation:

- The pair is chosen by the second order rule of Fan, Chen and Lin. Variable
  `i` maximises `-y_i G_i` over the set that may increase, and variable `j` is
  the partner that promises the largest drop in the objective.
- The gradient `G_i = sum_j Q_ij a_j - 1` is carried forward rather than
  recomputed, so each iteration costs two kernel rows.
- Kernel rows are cached on a least-recently-used list, sized by `-c` in
  megabytes and defaulting to 100. When the budget covers the whole matrix,
  every row simply stays resident.

Shrinking is not implemented. It is a speed optimisation, and it changes no
result.

The solver stops when `m(a) - M(a) <= eps`, with `eps` defaulting to 0.001 and
settable with `-e`. It gives up after `max(10^7, 100m)` iterations and reports
failure rather than looping.

## Validation

The solver was checked against `libsvm-weights-3.37`, the instance-weighted
LIBSVM by Chang, Lin, Tsai, Ho and Yu, rebuilt with a double-precision kernel
cache. On the sonar data with weights drawn between 0.07 and 4.94, across five
parameter sets covering both kernels and costs from 0.2 to 100, the two agree
bit for bit: the same iteration counts, the same objective, and differences of
exactly zero in both the multipliers and the threshold. Three of those five
runs leave many multipliers at their upper bound, which is where the weights
act.

LIBSVM caches kernel values as `float` by default. That, and only that,
accounts for the small disagreement seen against a stock build.

Scoring was checked the same way. Our decision values match those implied by
LIBSVM's own model file to 1.4e-8 across all 207 points, which is the residue
of the default stopping tolerance. On a 150 and 57 split of the sonar data, our
model and LIBSVM's agree exactly on the training run, both reach 84.21 per cent
accuracy on the held-out part, and they assign the same label to all 57 points.
The probabilities differ by at most 0.022, which is the fold assignment rather
than the method, and ours are well calibrated across all four quarters of the
range.

A second check uses no external code. An integer weight `k` must give the same
model as the same point repeated `k` times with weight 1, because the copies
share a gradient and divide the bound `kC` between them. Expanding 30 rows into
73 reproduces the multipliers and the threshold exactly, for both kernels.

## Layout

```
make-training.sh      creates an empty database from sql/ddl.sql
add-training-data.sh  loads one dataset
add-parameters.sh     loads parameter sets
train-svm.sh          runs smo, then records the model
add-problem.sh        loads data points to be classified
apply-svm.sh          runs score, which writes yhat
sql/ddl.sql           the schema
src/                  the C99 solver and scorer
data/                 example inputs
```

Inside `src`:

```
smo.c       the smo command line, which trains
score.c     the score command line, which classifies
db.c        reads datasets, models and problems; writes the results
solver.c    SMO
platt.c     the sigmoid and the cross-validation behind it
kernel.c    the kernels and the row cache
prob.h      the problem, the points and the model, as held in memory
util.c      die and allocation
```

The C is written in the suckless style that `.clang-format` encodes, the same
one the sibling `dataset` project uses.
