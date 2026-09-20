# Trees sandbox

This repo contains code I used to explore ideas. 

## Idea n. 1

The first idea as based on the Isolation Trees concept. 

Imagine we generate 100 points, randomly, over a polygon that was labelled as Forest. But
the Remotely Sensed Imagery has higher resolution, so where the map says "Forest" we can see
roads, rooftops, etc. But the majoriy is forestry, and thus correctly labelled. So where are the
points that are incorrectly labelled? They should be located in isolated neighbours of the 
feature space. 

The dataset is composed by the following schema `(x, y, v1, v2, ..., vN)` where `x` and `y` are
geographic coordinates, and `v1`, ..., `vN` are the `N` features. For the exercise we assume
the dataset variables are scaled from 0.0 to 1.0. 

Let's use binary partition of space, as in a Classification Tree, where a variability index is 
used to find the best cut. The space partition is executed until all data-points are in a leaf. 
Each node has the following attributes: 1) variable ID used in the cut and the cut value; 2) number
of data-points in the node; 3) the nodes mean vector; 4) the quadratic distance between the node's 
mean vector and the parent's mean vector; and 5) the depth of the node.

### Running it

```
$ make itree
$ ./data/make-mock.sh
$ ./itree -v -i data/mock.csv -o tree.csv
./itree: points=1000 dim=10 nq=10 criterion=weighted nodes=1999 leaves=1000 depth=19
```

`data/make-mock.sh` writes 1000 points over 10 variables in five groups: two
that overlap, two that are isolated, and 20 that sit on their own. It also
writes `mock-truth.csv`, naming each point's group, so the result can be
checked.

### The cut

For each variable the node's values are sorted and a cut is tried at each tenth
of the way along, so at 10 per cent, 20 per cent and so on. Each candidate is
scored by the spread of its two parts, where the spread of a part is

```
spread(S) = sqrt( (1/N) sum_j var_j(S) )
```

so one number covering all variables at once, not only the one being cut.
`-c weighted`, the default, takes the two spreads weighted by their size.
`-c min` takes the tighter side alone, which is the literal reading of "one
part is very concentrated". Candidates are scored from prefix sums along the
sorted order, so a node costs one pass per variable rather than one per
candidate.

### The output

One row per node, the row's position giving the node its index, so the root is
the first row after the header. The columns are `parent`, `left`, `right`,
`vid`, `cut`, `n`, the mean vector, then `sd_left` and `sd_right`. The root
carries -1 for its parent and a leaf carries -1 for its children, leaving the
cut and the two spreads empty. Depth is not a column, because walking the
`parent` column gives it, and the distance to the parent's mean follows from
the two mean vectors.

### What came out

Mean leaf depth by group, which is the isolation signal: a point reached in few
cuts is one that was easy to separate.

```
group     n   weighted     min
A       300      11.96   17.39
B       300      11.74   20.01
C       190      11.53   18.35
D       190      11.58   15.52
E        20       7.80   27.10
```

Under `weighted` the 20 singular points come out at 7.80 against about 12 for
the clustered ones, so a shallow leaf flags an outlier. That is the result the
idea predicts. Separation is not clean from one tree: of the 20 shallowest
points, four are from group E, which is a tenfold enrichment over its 2 per
cent share rather than a clean cut. An ensemble would be the next step.

Under `min` the ordering inverts and E becomes the deepest group. A small part
is tight almost by construction, so that criterion always rewards shaving a
thin dense slice off the nearest cluster. The scattered points are never that
slice, so they survive in the remainder and are resolved last. It is kept as an
option because it is worth seeing.

### How the code is organised

C99, no dependencies. `make` compiles, `make itree` links, `make fmt` runs
`clang-format` over the sources using the `.clang-format` here.

```
src/itree.c   the command line, and nothing else
src/csv.c     reads the data file into a dense matrix
src/tree.c    picks the cuts, builds the tree, writes it out
src/util.c    die and allocation
data/         make-mock.sh and the files it writes
```

`tree.c` holds the whole idea. The points are held in one index array that the
recursion partitions in place, so each node owns a contiguous range of it and
no set of points is ever copied. Nodes go into a growable array, which is why
the output can name children and parents by index directly.
