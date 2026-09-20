#!/bin/sh
#
# train-svm.sh - train a model and record it.
#
# usage: train-svm.sh [-v] -t training.sqlite3 -d dsid -p pid
#
# It runs smo, the C99 solver that sits next to this script, which reads
# dataset dsid under parameter set pid and writes the multipliers into
# coefficients and the threshold into bias.  This script then records the
# model in models and prints the model ID.
#
# One writer at a time is assumed: the mid is allocated, and only then
# used.

set -eu

argv0=${0##*/}
here=$(cd "$(dirname "$0")" && pwd)
smo=$here/smo

usage() {
	echo "usage: $argv0 [-v] -t training.sqlite3 -d dsid -p pid" >&2
	exit 1
}

die() {
	echo "$argv0: $*" >&2
	exit 1
}

isint() {
	printf '%s' "$1" | grep -Eq '^[0-9]+$'
}

db=
dsid=
pid=
verbose=
while [ $# -gt 0 ]; do
	case $1 in
	-v)
		verbose=-v
		;;
	-t)
		[ $# -ge 2 ] || usage
		db=$2
		shift
		;;
	-d)
		[ $# -ge 2 ] || usage
		dsid=$2
		shift
		;;
	-p)
		[ $# -ge 2 ] || usage
		pid=$2
		shift
		;;
	*)
		usage
		;;
	esac
	shift
done

[ -n "$db" ] && [ -n "$dsid" ] && [ -n "$pid" ] || usage
isint "$dsid" || die "dsid must be a non-negative integer, found $dsid"
isint "$pid" || die "pid must be a non-negative integer, found $pid"
[ -r "$db" ] || die "cannot read $db"
[ -x "$smo" ] || die "cannot find $smo; run make smo first"

for t in x y metadata parameters coefficients bias models problem yhat; do
	k=$(sqlite3 "$db" "SELECT count(*) FROM sqlite_master
	                   WHERE type = 'table' AND name = '$t'")
	[ "$k" = 1 ] || die "$db has no table $t; run make-training.sh first"
done

k=$(sqlite3 "$db" "SELECT count(*) FROM metadata WHERE dsid = $dsid")
[ "$k" = 1 ] || die "no dataset with dsid $dsid"
k=$(sqlite3 "$db" "SELECT count(*) FROM parameters WHERE pid = $pid")
[ "$k" = 1 ] || die "no parameter set with pid $pid"

out=$("$smo" ${verbose:+$verbose} -t "$db" -d "$dsid" -p "$pid")
cid=$(printf '%s\n' "$out" | sed -n 's/^cid=//p')
bid=$(printf '%s\n' "$out" | sed -n 's/^bid=//p')
isint "$cid" && isint "$bid" || die "smo returned no coefficient or bias ID"

mid=$(sqlite3 "$db" "SELECT coalesce(max(mid), 0) + 1 FROM models")
sqlite3 "$db" "INSERT INTO models (mid, dsid, pid, cid, bid, generated_at)
               VALUES ($mid, $dsid, $pid, $cid, $bid, datetime('now'))"

k=$(sqlite3 "$db" "SELECT count(*) FROM models WHERE mid = $mid")
[ "$k" = 1 ] || die "could not record the model"

echo "mid=$mid"
