#!/bin/sh
#
# add-parameters.sh - load parameter sets into an SVM training database.
#
# usage: add-parameters.sh -m parameters.csv -t training.sqlite3
#
# The file is delimited by "|" and holds a header row followed by one row
# per parameter set.  The columns are kernel, which may also be spelled
# kid and is 0 for linear or 1 for radial, cost, gamma, and comment,
# which may also be spelled comments and may itself contain "|".  Gamma
# is ignored when the kernel is linear, but it is still stored.
#
# Each row is given a fresh pid, and the pids are printed in order.  A
# parameter set does not belong to a dataset, so the same pid may be
# trained against several of them.
#
# One writer at a time is assumed: the first pid is allocated, and only
# then used.

set -eu

argv0=${0##*/}
tmp=

usage() {
	echo "usage: $argv0 -m parameters.csv -t training.sqlite3" >&2
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

params=
db=
while [ $# -gt 0 ]; do
	case $1 in
	-m)
		[ $# -ge 2 ] || usage
		params=$2
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

[ -n "$params" ] && [ -n "$db" ] || usage
for f in "$params" "$db"; do
	[ -r "$f" ] || die "cannot read $f"
done

for t in x y metadata parameters coefficients bias models problem yhat; do
	k=$(sqlite3 "$db" "SELECT count(*) FROM sqlite_master
	                   WHERE type = 'table' AND name = '$t'")
	[ "$k" = 1 ] || die "$db has no table $t; run make-training.sh first"
done

pid=$(sqlite3 "$db" "SELECT coalesce(max(pid), 0) + 1 FROM parameters")

trap cleanup EXIT HUP INT TERM
tmp=$(mktemp -d)

# Check every row, and only then write the rows to import.  The comment
# is quoted for CSV, so a comma or a quote inside it survives.
awk -F'[|]' -v first="$pid" -v f="$params" -v out="$tmp/p.csv" '
	function trim(s) {
		gsub(/^[ \t\r]+|[ \t\r]+$/, "", s)
		return s
	}
	function num(s) {
		return s ~ /^[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?$/
	}
	function pick(a, b,   i) {
		for (i in col)
			if (i == a)
				return col[a]
		return (b in col) ? col[b] : 0
	}
	/^[ \t\r]*$/ { next }
	!seen_header {
		hn = NF
		for (i = 1; i <= NF; i++)
			col[trim($i)] = i
		ck = pick("kernel", "kid")
		cc = pick("cost", "cost")
		cg = pick("gamma", "gamma")
		cm = pick("comment", "comments")
		if (!ck || !cc || !cg) {
			printf "%s: header needs kernel, cost and gamma\n",
			    f > "/dev/stderr"
			bad++
			exit 1
		}
		seen_header = 1
		next
	}
	{
		rows++
		k = trim($ck)
		c = trim($cc)
		g = trim($cg)
		if (k != "0" && k != "1") {
			printf "%s:%d: kernel must be 0 or 1, found %s\n",
			    f, NR, k > "/dev/stderr"
			bad++
		}
		if (!num(c) || c + 0 <= 0) {
			printf "%s:%d: cost must be a positive number, found %s\n",
			    f, NR, c > "/dev/stderr"
			bad++
		}
		if (!num(g)) {
			printf "%s:%d: gamma must be a number, found %s\n",
			    f, NR, g > "/dev/stderr"
			bad++
		} else if (k == "1" && g + 0 <= 0) {
			printf "%s:%d: gamma must be positive for a radial kernel, found %s\n",
			    f, NR, g > "/dev/stderr"
			bad++
		}
		note = ""
		if (cm) {
			note = $cm
			# Only the last column absorbs a stray "|".
			if (cm == hn)
				for (i = cm + 1; i <= NF; i++)
					note = note "|" $i
			note = trim(note)
		}
		gsub(/"/, "\"\"", note)
		printf "%d,%s,%s,%s,\"%s\"\n", first + rows - 1, k, c, g,
		    note > out
	}
	END {
		if (!rows)
			printf "%s: holds no parameter rows\n", f > "/dev/stderr"
		exit (bad > 0 || rows == 0)
	}
' "$params" || die "$params is not usable"

rows=$(wc -l < "$tmp/p.csv" | tr -d ' ')

sqlite3 "$db" <<SQL
BEGIN;
.import --csv "$tmp/p.csv" parameters
COMMIT;
SQL

last=$((pid + rows - 1))
got=$(sqlite3 "$db" "SELECT count(*) FROM parameters
                     WHERE pid BETWEEN $pid AND $last")
if [ "$got" != "$rows" ]; then
	sqlite3 "$db" "DELETE FROM parameters
	               WHERE pid BETWEEN $pid AND $last"
	die "load failed: expected $rows rows, stored $got"
fi

i=$pid
while [ "$i" -le "$last" ]; do
	echo "pid=$i"
	i=$((i + 1))
done
