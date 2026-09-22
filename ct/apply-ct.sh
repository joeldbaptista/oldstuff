#!/bin/sh
#
# apply-ct.sh - drop a problem down a tree and print the class of every
# data point.
#
# usage: apply-ct.sh [-v] -t training.sqlite3 -o tree.sqlite3 -p prid
#
# It attaches the tree to the training database and walks each data
# point from the root to a leaf, taking the left child where the value
# of the cut variable is at most the cut value and the right child
# otherwise.  The class of a data point is the majority class of the
# leaf it reaches.  One row per data point is printed, as
#
#     dpid|class
#
# in dpid order.  Nothing is written, because the schema keeps no table
# for the result; redirect the output to a file, or import it into a
# table of your own.  With -v the number of data points classified is
# reported on stderr.
#
# A data point with no row for a variable takes the value 0.0 there,
# which is what the triple store means by leaving it out.

set -eu

argv0=${0##*/}

usage() {
	echo "usage: $argv0 [-v] -t training.sqlite3 -o tree.sqlite3 -p prid" >&2
	exit 1
}

die() {
	echo "$argv0: $*" >&2
	exit 1
}

isint() {
	printf '%s' "$1" | grep -Eq '^[0-9]+$'
}

quote() {
	printf "'%s'" "$(printf '%s' "$1" | sed "s/'/''/g")"
}

db=
tree=
prid=
verbose=0
while [ $# -gt 0 ]; do
	case $1 in
	-v)
		verbose=1
		;;
	-t)
		[ $# -ge 2 ] || usage
		db=$2
		shift
		;;
	-o)
		[ $# -ge 2 ] || usage
		tree=$2
		shift
		;;
	-p)
		[ $# -ge 2 ] || usage
		prid=$2
		shift
		;;
	*)
		usage
		;;
	esac
	shift
done

[ -n "$db" ] && [ -n "$tree" ] && [ -n "$prid" ] || usage
isint "$prid" || die "prid must be a non-negative integer, found $prid"
for f in "$db" "$tree"; do
	[ -r "$f" ] || die "cannot read $f"
done

for t in x y metadata problem; do
	k=$(sqlite3 "$db" "SELECT count(*) FROM sqlite_master
	                   WHERE type = 'table' AND name = '$t'")
	[ "$k" = 1 ] || die "$db has no table $t; run make-training.sh first"
done
for t in ct node_distribution; do
	k=$(sqlite3 "$tree" "SELECT count(*) FROM sqlite_master
	                     WHERE type = 'table' AND name = '$t'")
	[ "$k" = 1 ] || die "$tree has no table $t; run build-ct first"
done

k=$(sqlite3 "$db" "SELECT count(DISTINCT dpid) FROM problem
                   WHERE prid = $prid")
[ "$k" -ge 1 ] || die "no problem with prid $prid"

# Every variable the tree cuts on must be one the problem carries, or
# the walk would read a missing row as 0.0 and answer nonsense.
k=$(sqlite3 "$db" "ATTACH $(quote "$tree") AS t;
                   SELECT count(DISTINCT c.cutvar) FROM t.ct c
                   WHERE c.cutvar IS NOT NULL AND c.cutvar NOT IN
                       (SELECT DISTINCT vid FROM problem
                        WHERE prid = $prid)")
[ "$k" = 0 ] ||
	die "the tree cuts on $k variables the problem never mentions;" \
	    "it was built for a different dataset"

out=$(sqlite3 "$db" <<SQL
ATTACH $(quote "$tree") AS t;
WITH RECURSIVE walk(dpid, nidx) AS (
	SELECT DISTINCT dpid, 1 FROM problem WHERE prid = $prid
	UNION ALL
	SELECT w.dpid,
	       CASE WHEN coalesce(p.val, 0.0) <= c.cutval THEN c.lidx
	            ELSE c.ridx END
	FROM walk w
	JOIN t.ct c ON c.nidx = w.nidx AND c.cutvar IS NOT NULL
	LEFT JOIN problem p ON p.prid = $prid AND p.dpid = w.dpid
	                   AND p.vid = c.cutvar
)
SELECT w.dpid || '|' || c.majority
FROM walk w
JOIN t.ct c ON c.nidx = w.nidx
WHERE c.cutvar IS NULL
ORDER BY w.dpid;
SQL
)

# Every data point must reach a leaf, so count what came back.
n=$(printf '%s\n' "$out" | grep -c '|' || true)
got=$(sqlite3 "$db" "SELECT count(DISTINCT dpid) FROM problem
                     WHERE prid = $prid")
[ "$n" = "$got" ] ||
	die "walked $n of the $got data points; the tree is not whole"

printf '%s\n' "$out"
[ "$verbose" -eq 1 ] && echo "$argv0: classified=$n" >&2
exit 0
