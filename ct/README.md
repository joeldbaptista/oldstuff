# Classification tree

This implements a Classification Tree. I assumes the following:

1. There are N classes labelled: 1, 2, ..., N;
2. All feature variables are continuous variables;

This is the CLI:

```
$ ./build-ct -i training.sqlite3 -o tree.sqlite3 -m <metric-index>
```

Where `trainig.sqlite3` is the training set as described in `svm`, `metric-index` 
is the metric utilised to perform the cut; it can be the Gini index, or Entropy. 
This produces a SQLite3 file, called `tree.sqlite3`, with the following tables:

```SQL
CREATE TABLE ct (
	nidx INTEGER, 	/* node index */
	lidx INTEGER,	/* index of the left child */
	ridx INTEGER,	/* index of the right child */
	pidx INTEGER,	/* the index of the parent; NULL for the root */
	cutvar INTEGER, /* cut variable */
	cutval DOUBLE PRECISION, /* cut value */
	size INTEGER, 	/* number of data points in the node */
	majority INTEGER, /* most frequent class */
	level INTEGER 	/* level of the node; 0 at the root */
);

CREATE TABLE node_distribution (
	nidx INTEGER,		/* node ID */
	class_id INTEGER,	/* class ID: 1, 2, ..., N */
	count INTEGER 		/* number of elements of class `class_id` in node `nidx` */
);
```

## Introduction

Everything lives in SQLite3. The shell scripts fill a training database, one
C99 program reads it and writes a tree, and the tree is a database of its own.
Nothing is written outside those two files.

The pipeline runs in five steps:

```
make-training.sh       an empty training database
add-training-data.sh   labelled data        -> dsid
build-ct               a dataset + a metric -> tree.sqlite3
add-problem.sh         unlabelled data      -> prid
apply-ct.sh            prid + a tree        -> one class per data point
```

Only `build-ct` is C. Classifying is a walk from the root to a leaf, which is
one recursive query, so `apply-ct.sh` is shell and SQL and no more.

The two metric indexes are 0 for the Gini index and 1 for entropy. The Cut
section below gives both.

The method is the one of Breiman, Friedman, Olshen and Stone,
*Classification and Regression Trees*, Wadsworth, 1984, which is where the
binary cut on one variable, the Gini index and the impurity decrease all come
from. Two parts of that book are missing here, and this list is complete: the
cost complexity pruning of its chapter 3, and the surrogate splits it uses for
a missing value. So what is implemented is their growing rule, and not the
whole of CART.

## The two databases

The training database is the one the sibling `svm` project describes, with one
change and one omission. The change is `y.y`, which holds a class label 1, 2,
..., N rather than -1 or +1, because a tree is not restricted to two classes.
The omission is the tables that belong to an SVM and to nothing else, being
`parameters`, `coefficients`, `bias`, `models` and `yhat`; a tree carries no
parameter set and no coefficients, and this project stores no classification.
So four tables remain:

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
```

A dataset is identified by `dsid`. Within a dataset, `dpid` numbers the data
points and `vid` numbers the variables from 0 to `n - 1`. A row that is absent
from `x` means the value 0.0, so a sparse dataset costs only the rows it
actually needs.

The weight `y.w` is kept so that a dataset written for `svm` loads here
unchanged, but `build-ct` ignores it. Counting is what a tree does, and the
counts it stores are integers.

A problem is a set of unlabelled data points to be classified, and it is
identified by `prid`. Its rows have the same shape as those of `x`, so an
absent row means 0.0, and `vid` must match the variables of the dataset the
tree was grown on.

`sql/ddl.sql` adds two things the listing above does not show, and this list is
complete: the defaults already shown in the comments, and indexes. The indexes
on `x (dsid, dpid, vid)`, `y (dsid, dpid)`, `metadata (dsid)` and
`problem (prid, dpid, vid)` are unique, so loading the same thing twice is
rejected rather than silently doubled.

The tree database holds the two tables given at the top of this file, and
`build-ct` creates it. Six conventions matter when reading them, and this list
is complete:

- Nodes are numbered from 1 in the order they were built, so the root is node
  1 and a child always carries a higher `nidx` than its parent.
- A leaf stores no children and no cut, so `lidx`, `ridx`, `cutvar` and
  `cutval` are NULL there. `cutvar IS NULL` is therefore the test for a leaf.
- The root stores no parent, so `pidx` is NULL there, and only there.
- `cutvar` is a `vid` of the dataset, counted from 0. A data point goes left
  where its value of that variable is at most `cutval`, and right otherwise.
- `level` is the level of the node, counting the root as 0, so the level of a
  node is one above the level of its parent and the largest level in the table
  is the depth of the tree.
- `node_distribution` carries a row only for a class the node actually holds.
  A count of zero says nothing, so it is left out, and the counts of a node sum
  to its `size`.

The two tables are indexed on `ct (nidx)`, which is unique, on `ct (pidx)`, and
on `node_distribution (nidx, class_id)`, which is unique.

## Input files

Both input files are delimited by `|`, not by a comma.

The data file holds one data point per row and `n + 2` fields per row, with no
header. Field 1 is the class label, which is one of 1, 2, ..., N. Field 2 is
the weight, which must be positive and which `build-ct` then ignores. Fields 3
onwards are the variables, filed under `vid` 0 to `n - 1`. Data points are
numbered from 0 in the order they are read.

```
1|1.0|5.1|3.5|1.4|0.2
2|1.0|7.0|3.2|4.7|1.4
```

The weight is kept in the file for one reason only: a data file written for
`svm` then loads here unchanged, once its labels are rewritten.

The metadata file holds a header row and one data row. Its columns are `m`, the
number of rows, `n`, the number of variables, and `comment`, which may also be
spelled `comments`.

```
m|n|comments
114|4|iris, training part
```

The problem file holds one data point per row and no header. Every field is the
value of one variable, filed under `vid` 0 onwards. There is no label and no
weight, which is what separates a problem from a training set.

```
4.6|3.1|1.5|0.2
5.0|3.4|1.5|0.2
```

The labels must run 1, 2, ..., N with no gap. `add-training-data.sh` rejects a
file that reaches class N without carrying a row of every class below it,
because such a file was labelled some other way, and the empty class would then
sit in every node distribution saying nothing.

## Building

```
$ make          # compiles src/*.o
$ make bin      # links ./build-ct, which the scripts sit beside
```

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

Load the labelled data:

```
$ ./add-training-data.sh -d data/iris-train.csv \
      -m data/iris-train-metadata.csv -t training.sqlite3
dsid=1
```

Grow the tree:

```
$ ./build-ct -i training.sqlite3 -o tree.sqlite3 -m 0
nodes=13
leaves=7
```

The full command line is

```
$ ./build-ct [-v] [-f] [-d dsid] [-s minsplit] [-l minleaf] [-D maxdepth] \
      -i training.sqlite3 -o tree.sqlite3 -m metric
```

and the options are these, which is a complete list:

- `-m` is the metric, 0 for the Gini index and 1 for entropy. It is required.
- `-d` names the dataset. A training database holding one dataset needs no
  `-d`, and one holding several is an error without it.
- `-s` is the smallest node a cut may be tried on, and it defaults to 2.
- `-l` is the smallest part a cut may leave, and it defaults to 1. `-s` must be
  at least twice `-l`, since a cut leaves two parts.
- `-D` is the deepest node allowed, counting the root as 0. Without it the tree
  grows until no cut is left to make.
- `-f` overwrites an existing tree file. Without it, an existing file is an
  error.
- `-v` writes one line on stderr naming the dataset, its shape, the metric, the
  limits and the size of the tree.

Load a set of data points to be classified:

```
$ ./add-problem.sh -i data/iris-test.csv -t training.sqlite3
prid=1
```

Classify them with the tree:

```
$ ./apply-ct.sh -t training.sqlite3 -o tree.sqlite3 -p 1
0|1
1|1
...
```

One row per data point is printed, being the `dpid` and the class, in `dpid`
order. Nothing is written, because the schema keeps no table for the result.
Add `-v` for the count on stderr.

Nothing is written when a step fails. Each tool checks its whole input before
it touches a database, and the writes sit inside one transaction.

## A worked test

`data/iris.csv` is the iris data, 150 points over four variables in three
classes of 50. It carries a header and one data point per row, the four
variables first and the class label last.

`data/split-iris.sh` cuts it into two parts. The cut is deterministic and
stratified: within each class, in file order, every fourth data point goes to
the test part. So the class balance holds, and the files can be regenerated at
any time.

```
$ ./data/split-iris.sh
train 114, test 36, variables 4
```

It writes four files, and this list is complete: `iris-train.csv` and
`iris-train-metadata.csv` for `add-training-data.sh`, `iris-test.csv` for
`add-problem.sh`, and `iris-test-labels.csv`, which holds the true class of
each test point so that the result can be checked.

### Running it

```
$ make bin
$ ./make-training.sh -o test.sqlite3
$ ./add-training-data.sh -d data/iris-train.csv \
      -m data/iris-train-metadata.csv -t test.sqlite3
dsid=1
$ ./add-problem.sh -i data/iris-test.csv -t test.sqlite3
prid=1
$ ./build-ct -v -i test.sqlite3 -o gini.sqlite3 -m 0
./build-ct: dsid=1 m=114 n=4 classes=3 metric=gini minsplit=2 minleaf=1 nodes=13 leaves=7 depth=4
nodes=13
leaves=7
$ ./build-ct -v -i test.sqlite3 -o entropy.sqlite3 -m 1
./build-ct: dsid=1 m=114 n=4 classes=3 metric=entropy minsplit=2 minleaf=1 nodes=13 leaves=7 depth=4
nodes=13
leaves=7
```

### The tree

```
$ sqlite3 -header -column gini.sqlite3 'SELECT * FROM ct'
nidx  lidx  ridx  pidx  cutvar  cutval  size  majority  level
----  ----  ----  ----  ------  ------  ----  --------  -----
1     2     3           2       2.45    114   1         0
2                 1                     38    1         1
3     4     9     1     3       1.65    76    2         1
4     5     6     3     2       5.0     40    2         2
5                 4                     36    2         3
6     7     8     4     0       6.05    4     3         3
7                 6                     1     2         4
8                 6                     3     3         4
9     10    13    3     2       4.85    36    3         2
10    11    12    9     1       3.1     4     3         3
11                10                    3     3         4
12                10                    1     2         4
13                9                     32    3         3
```

The root cuts on `vid` 2, the petal length, at 2.45, and node 2 is the 38
points of class 1, pure at the first cut. That is the well known shape of the
iris data. The remaining 76 points take four further cuts to separate, and two
of the leaves hold a single point apiece, which is what growing without pruning
does.

The distribution of any node reads like this:

```
$ sqlite3 -header -column gini.sqlite3 \
      'SELECT * FROM node_distribution WHERE nidx = 3'
nidx  class_id  count
----  --------  -----
3     2         38
3     3         38
```

### Reading the result

The true classes sit outside the database, in `data/iris-test-labels.csv`,
because the schema keeps no table for the truth of a problem. So write the
classification to a file, load both into temporary tables, and join:

```sh
$ ./apply-ct.sh -t test.sqlite3 -o gini.sqlite3 -p 1 > gini-pred.csv
$ sqlite3 test.sqlite3 <<'SQL'
CREATE TEMP TABLE truth (dpid INTEGER, y INTEGER);
CREATE TEMP TABLE pred (dpid INTEGER, yhat INTEGER);
.mode csv
.separator |
.import --skip 1 data/iris-test-labels.csv truth
.import gini-pred.csv pred
.mode column
.headers on
SELECT t.y AS class, sum(p.yhat = 1) AS as_1, sum(p.yhat = 2) AS as_2,
       sum(p.yhat = 3) AS as_3, count(*) AS total
FROM truth t JOIN pred p ON p.dpid = t.dpid
GROUP BY t.y ORDER BY t.y;

SELECT sum(p.yhat = t.y) AS correct, count(*) AS total,
       round(100.0 * sum(p.yhat = t.y) / count(*), 2) AS accuracy
FROM truth t JOIN pred p ON p.dpid = t.dpid;
SQL
```

```
class  as_1  as_2  as_3  total
-----  ----  ----  ----  -----
1      12    0     0     12
2      0     11    1     12
3      0     1     11    12

correct  total  accuracy
-------  -----  --------
34       36     94.44
```

So 34 of the 36 held-out points are placed correctly, which is 94.44 per cent,
and the two errors are one class 2 called class 3 and one class 3 called class
2. Class 1 is separated perfectly, which the first cut already told us.

The entropy tree gives the same table on this data. The two metrics agree on
every cut here, down to the cut values, so they classify the 36 test points
identically. That is a property of this dataset and not of the two metrics,
which can and do disagree elsewhere.

## The cut

A node is cut on one variable at one value. The rows whose value of that
variable is at most the cut value go left, and the rest go right. The cut kept
is the one that leaves the two parts as pure as it can, purity being measured
by the metric:

```
gini(S)    = 1 - sum_k p_k^2
entropy(S) = - sum_k p_k log2 p_k
```

where `p_k` is the share of class k in the part. Both are zero on a part of one
class and largest when the classes are level, so the two agree on what a pure
node is and differ only in how hard they punish a mixed one.

A candidate is scored by the size weighted impurity of its two parts,

```
score = (nl * I(left) + nr * I(right)) / n
```

and the cut with the smallest score wins. That score can never be above the
impurity of the node itself, so the difference between the two is the gain, and
a cut is kept only when the gain is positive.

The search is exhaustive. For each variable the node's rows are sorted on that
variable, the class counts of the left part are carried along the sorted order,
and every boundary between two distinct values is scored from those counts. So
a node costs one sort and one pass per variable, rather than one pass per
candidate, and no candidate is missed. The cut value itself is the midpoint of
the two values the boundary separates.

Growing stops at a node that meets any one of four conditions, and this list is
complete: the node is pure; the node is smaller than `minsplit`; the node sits
at `maxdepth`; or no cut has a positive gain. The last of those covers a node
whose rows are identical in every variable but not in class, which cannot be
cut at all. Such a node is a leaf holding more than one class, and its
`majority` is then a genuine majority rather than the whole of it.

Two runs on the same data give the same tree. Ties are broken by taking the
first cut found, and the search runs over the variables in order and over the
values in ascending order, so "first" is a fixed thing. Equal values are never
separated, so the order in which the sort leaves them does not reach the
result. A tie for the majority class goes to the lower class label.

There is no pruning. The tree grows until one of the four conditions stops it,
and `-s`, `-l` and `-D` are the only limits on offer. Breiman, Friedman, Olshen
and Stone argue in chapter 3 of *Classification and Regression Trees* that a
stopping rule is the weaker remedy, and that growing a large tree and then
pruning it back by cost complexity is the sounder one. That is the piece this
implementation leaves out.

## Classifying

`apply-ct.sh` attaches the tree to the training database and walks every data
point of a problem from the root to a leaf. The walk is one recursive query:

```SQL
WITH RECURSIVE walk(dpid, nidx) AS (
	SELECT DISTINCT dpid, 1 FROM problem WHERE prid = ?
	UNION ALL
	SELECT w.dpid,
	       CASE WHEN coalesce(p.val, 0.0) <= c.cutval THEN c.lidx
	            ELSE c.ridx END
	FROM walk w
	JOIN t.ct c ON c.nidx = w.nidx AND c.cutvar IS NOT NULL
	LEFT JOIN problem p ON p.prid = ? AND p.dpid = w.dpid
	                   AND p.vid = c.cutvar
)
SELECT w.dpid, c.majority
FROM walk w
JOIN t.ct c ON c.nidx = w.nidx
WHERE c.cutvar IS NULL
ORDER BY w.dpid;
```

The class of a data point is the majority class of the leaf it reaches. The
join is a left join because an absent row means 0.0, as in `x`, and the walk
must reach a leaf either way.

A tree that cuts on a variable the problem never mentions is refused rather
than walked. Absent rows mean 0.0, so such a problem would be classified on
zeros and the answer would be nonsense. That is the same refusal `svm` makes
when a problem stops short of `n - 1`.

## Limits

Four things this does not do, and this list is complete:

- It ignores the data point weight. `y.w` is read and checked by the loader,
  because `svm` writes it, and then nothing uses it.
- It does not prune. A tree grown to purity fits the training part more closely
  than it fits anything else, and `-D`, `-s` and `-l` are the only remedies
  here. Cost complexity pruning, which is the remedy Breiman, Friedman, Olshen
  and Stone give, is not implemented.
- It treats every variable as continuous, which is the second assumption at the
  top of this file. A categorical variable coded as a number would be cut as
  though its codes were ordered.
- It knows nothing of a missing value. An absent row in `x` is the value 0.0
  and not a gap.

One implementation limit is worth naming as well: the build recurses once per
node down a branch, so the C stack carries the depth of the tree. Every dataset
that separates at a reasonable rate is far from any trouble there, and the
deepest branch of the iris tree is 4.

## Layout

```
make-training.sh      creates an empty training database from sql/ddl.sql
add-training-data.sh  loads one dataset
build-ct              grows the tree and writes it
add-problem.sh        loads data points to be classified
apply-ct.sh           walks a problem down a tree
sql/ddl.sql           the schema of the training database
src/                  the C99 tree builder
data/                 the iris data, and split-iris.sh
```

Inside `src`:

```
build-ct.c  the command line
tree.c      the cut, the recursion and the stopping rules
db.c        reads a dataset; creates and writes the tree database
data.h      the training set as held in memory
util.c      die and allocation
```

The C is written in the suckless style that `.clang-format` encodes, the same
one the sibling `svm` and `dataset` projects use.
