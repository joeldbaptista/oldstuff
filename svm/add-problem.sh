#!/bin/sh
#
# add-problem.sh - load a set of unlabelled data points to be classified.
#
# usage: add-problem.sh -i problem.csv -t training.sqlite3
#
# The file is delimited by "|" and holds no header.  Each row is one data
# point, and every field is the value of one variable, filed under vid 0
# onwards.  There is no label and no weight, which is what separates a
# problem from a training set.  Data points are numbered from 0 in the
# order they are read.
#
# Every row must carry the same number of fields, and that number must
# match the n of the dataset a model was trained on before that model can
# score the problem.
#
# The prid allocated is printed.  One writer at a time is assumed.

set -eu

argv0=${0##*/}
tmp=

usage() {
	echo "usage: $argv0 -i problem.csv -t training.sqlite3" >&2
	exit 1
}

die() {
	echo "$argv0: $*" >&2
	exit 1
}

cleanup() {
	[ -n "$tmp" ] && rm -rf "$tmp"
	return 0
}

input=
db=
while [ $# -gt 0 ]; do
	case $1 in
	-i)
		[ $# -ge 2 ] || usage
		input=$2
		shift
		;;
	-t)
		[ $# -ge 2 ] || usage
		db=$2
		shift
		;;
	*)
		usage
		;;
	esac
	shift
done

[ -n "$input" ] && [ -n "$db" ] || usage
for f in "$input" "$db"; do
	[ -r "$f" ] || die "cannot read $f"
done

for t in x y metadata parameters coefficients bias models problem yhat; do
	k=$(sqlite3 "$db" "SELECT count(*) FROM sqlite_master
	                   WHERE type = 'table' AND name = '$t'")
	[ "$k" = 1 ] || die "$db has no table $t; run make-training.sh first"
done

# Check the whole file before touching the database.
awk -F'[|]' -v f="$input" '
	function trim(s) {
		gsub(/^[ \t\r]+|[ \t\r]+$/, "", s)
		return s
	}
	/^[ \t\r]*$/ { next }
	{
		rows++
		if (!n)
			n = NF
		else if (NF != n) {
			printf "%s:%d: expected %d fields, found %d\n",
			    f, NR, n, NF > "/dev/stderr"
			bad++
			next
		}
		for (i = 1; i <= NF; i++) {
			v = trim($i)
			if (v !~ /^[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?$/) {
				printf "%s:%d: field %d is not a number: %s\n",
				    f, NR, i, v > "/dev/stderr"
				bad++
			}
		}
	}
	END {
		if (!rows)
			printf "%s: holds no data points\n", f > "/dev/stderr"
		exit (bad > 0 || rows == 0)
	}
' "$input" || die "$input is not usable"

prid=$(sqlite3 "$db" "SELECT coalesce(max(prid), 0) + 1 FROM problem")

trap cleanup EXIT HUP INT TERM
tmp=$(mktemp -d)

awk -F'[|]' -v prid="$prid" -v out="$tmp/p.csv" '
	function trim(s) {
		gsub(/^[ \t\r]+|[ \t\r]+$/, "", s)
		return s
	}
	BEGIN { dpid = 0 }
	/^[ \t\r]*$/ { next }
	{
		for (i = 1; i <= NF; i++)
			printf "%d,%d,%d,%s\n", prid, dpid, i - 1,
			    trim($i) > out
		dpid++
	}
' "$input"

sqlite3 "$db" <<SQL
BEGIN;
.import --csv "$tmp/p.csv" problem
COMMIT;
SQL

want=$(wc -l < "$tmp/p.csv" | tr -d ' ')
got=$(sqlite3 "$db" "SELECT count(*) FROM problem WHERE prid = $prid")
if [ "$got" != "$want" ]; then
	sqlite3 "$db" "DELETE FROM problem WHERE prid = $prid"
	die "load failed: expected $want rows, stored $got"
fi

echo "prid=$prid"
