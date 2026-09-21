#!/bin/sh
#
# add-training-data.sh - load one dataset into an SVM training database.
#
# usage: add-training-data.sh -d data.csv -m metadata.csv \
#            -t training.sqlite3
#
# Each run allocates a fresh dsid, writes one metadata row and the data
# itself into x and y, then prints the dsid it allocated.
#
# Every input file is delimited by "|".
#
# The data file holds one data point per row and n + 2 fields per row,
# with no header: field 1 is the label, which is -1 or +1, field 2 is the
# weight, and fields 3 onwards are the variables, filed under vid 0 to
# n - 1.  Data points are numbered from 0 in the order read.
#
# The metadata file holds a header row and one data row.  The columns are
# m, the number of rows, n, the number of variables, and comment, which
# may also be spelled comments and may itself contain "|".
#
# One writer at a time is assumed: the dsid is allocated, and only then
# used.

set -eu

argv0=${0##*/}
tmp=

usage() {
	echo "usage: $argv0 -d data.csv -m metadata.csv -t training.sqlite3" >&2
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

# column file name... - print the named column of the single data row,
# taking the first name that the header carries.  A value in the last
# column may contain "|", so that column absorbs the rest of the line.
column() {
	f=$1
	shift
	awk -F'[|]' -v names="$*" '
		function trim(s) {
			gsub(/^[ \t\r]+|[ \t\r]+$/, "", s)
			return s
		}
		NR == 1 {
			hn = NF
			for (i = 1; i <= NF; i++)
				col[trim($i)] = i
			split(names, want, " ")
			for (k = 1; k in want; k++)
				if (want[k] in col) {
					c = col[want[k]]
					break
				}
			next
		}
		NR == 2 && c {
			v = $c
			# Only the last column absorbs a stray "|".
			if (c == hn)
				for (i = c + 1; i <= NF; i++)
					v = v "|" $i
			print trim(v)
		}
	' "$f"
}

isint() {
	printf '%s' "$1" | grep -Eq '^[+-]?[0-9]+$'
}

quote() {
	printf "'%s'" "$(printf '%s' "$1" | sed "s/'/''/g")"
}

data=
meta=
db=
while [ $# -gt 0 ]; do
	case $1 in
	-d)
		[ $# -ge 2 ] || usage
		data=$2
		shift
		;;
	-m)
		[ $# -ge 2 ] || usage
		meta=$2
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

[ -n "$data" ] && [ -n "$meta" ] && [ -n "$db" ] || usage
for f in "$data" "$meta" "$db"; do
	[ -r "$f" ] || die "cannot read $f"
done

for t in x y metadata parameters coefficients bias models problem yhat; do
	k=$(sqlite3 "$db" "SELECT count(*) FROM sqlite_master
	                   WHERE type = 'table' AND name = '$t'")
	[ "$k" = 1 ] || die "$db has no table $t; run make-training.sh first"
done

m=$(column "$meta" m)
n=$(column "$meta" n)
note=$(column "$meta" comment comments)

isint "$m" && [ "$m" -ge 1 ] || die "$meta: m must be a positive integer"
isint "$n" && [ "$n" -ge 1 ] || die "$meta: n must be a positive integer"

# Check the whole data file before touching the database.
awk -F'[|]' -v n="$n" -v want="$m" -v f="$data" '
	function trim(s) {
		gsub(/^[ \t\r]+|[ \t\r]+$/, "", s)
		return s
	}
	/^[ \t\r]*$/ { next }
	{
		seen++
		if (NF != n + 2) {
			printf "%s:%d: expected %d fields, found %d\n",
			    f, NR, n + 2, NF > "/dev/stderr"
			bad++
			next
		}
		lab = trim($1)
		if (lab != "-1" && lab != "1" && lab != "+1") {
			printf "%s:%d: label must be -1 or +1, found %s\n",
			    f, NR, lab > "/dev/stderr"
			bad++
		}
		for (i = 2; i <= NF; i++) {
			v = trim($i)
			if (v !~ /^[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?$/) {
				printf "%s:%d: field %d is not a number: %s\n",
				    f, NR, i, v > "/dev/stderr"
				bad++
			}
		}
		if (trim($2) + 0 <= 0) {
			printf "%s:%d: weight must be positive, found %s\n",
			    f, NR, trim($2) > "/dev/stderr"
			bad++
		}
	}
	END {
		if (seen != want)
			printf "%s: metadata claims %d rows, but the file holds %d\n",
			    f, want, seen > "/dev/stderr"
		exit (bad > 0 || seen != want)
	}
' "$data" || die "$data is not usable"

dsid=$(sqlite3 "$db" "SELECT coalesce(max(dsid), 0) + 1 FROM metadata")

trap cleanup EXIT HUP INT TERM
tmp=$(mktemp -d)

awk -F'[|]' -v dsid="$dsid" -v n="$n" -v xf="$tmp/x.csv" -v yf="$tmp/y.csv" '
	function trim(s) {
		gsub(/^[ \t\r]+|[ \t\r]+$/, "", s)
		return s
	}
	BEGIN { dpid = 0 }
	/^[ \t\r]*$/ { next }
	{
		printf "%d,%d,%d,%s\n", dsid, dpid, trim($1) + 0,
		    trim($2) > yf
		for (i = 1; i <= n; i++)
			printf "%d,%d,%d,%s\n", dsid, dpid, i - 1,
			    trim($(i + 2)) > xf
		dpid++
	}
' "$data"

sqlite3 "$db" <<SQL
BEGIN;
INSERT INTO metadata (dsid, m, n, comment)
VALUES ($dsid, $m, $n, $(quote "$note"));
.import --csv "$tmp/x.csv" x
.import --csv "$tmp/y.csv" y
COMMIT;
SQL

# .import does not report failure through the exit status, so count.
got=$(sqlite3 "$db" "SELECT (SELECT count(*) FROM x WHERE dsid = $dsid)
                         || '|'
                         || (SELECT count(*) FROM y WHERE dsid = $dsid)")
if [ "$got" != "$((m * n))|$m" ]; then
	sqlite3 "$db" "BEGIN;
	               DELETE FROM x WHERE dsid = $dsid;
	               DELETE FROM y WHERE dsid = $dsid;
	               DELETE FROM metadata WHERE dsid = $dsid;
	               COMMIT;"
	die "load failed: expected $((m * n))|$m rows, stored $got"
fi

echo "dsid=$dsid"
