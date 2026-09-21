#!/bin/sh
#
# apply-logreg.sh - classify a problem with a fitted model.
#
# usage: apply-logreg.sh [-v] [-f] -p prid -m mid -t training.sqlite3
#
# It runs predict, the C99 program that sits next to this script, which
# evaluates the fitted probability at every data point of the problem and
# writes one yhat row for each.  The number of data points classified is
# printed.
#
# A problem may be scored once by any one model, so re-scoring needs -f,
# which drops the earlier rows first.

set -eu

argv0=${0##*/}
here=$(cd "$(dirname "$0")" && pwd)
scorer=$here/predict

usage() {
	echo "usage: $argv0 [-v] [-f] -p prid -m mid -t training.sqlite3" >&2
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
prid=
mid=
verbose=
force=
while [ $# -gt 0 ]; do
	case $1 in
	-v)
		verbose=-v
		;;
	-f)
		force=-f
		;;
	-t)
		[ $# -ge 2 ] || usage
		db=$2
		shift
		;;
	-p)
		[ $# -ge 2 ] || usage
		prid=$2
		shift
		;;
	-m)
		[ $# -ge 2 ] || usage
		mid=$2
		shift
		;;
	*)
		usage
		;;
	esac
	shift
done

[ -n "$db" ] && [ -n "$prid" ] && [ -n "$mid" ] || usage
isint "$prid" || die "prid must be a non-negative integer, found $prid"
isint "$mid" || die "mid must be a non-negative integer, found $mid"
[ -r "$db" ] || die "cannot read $db"
[ -x "$scorer" ] || die "cannot find $scorer; run make bin first"

for t in x y metadata parameters coefficients bias models problem yhat; do
	k=$(sqlite3 "$db" "SELECT count(*) FROM sqlite_master
	                   WHERE type = 'table' AND name = '$t'")
	[ "$k" = 1 ] || die "$db has no table $t; run make-training.sh first"
done

k=$(sqlite3 "$db" "SELECT count(*) FROM models WHERE mid = $mid")
[ "$k" = 1 ] || die "no model with mid $mid"
k=$(sqlite3 "$db" "SELECT count(DISTINCT dpid) FROM problem
                   WHERE prid = $prid")
[ "$k" -ge 1 ] || die "no problem with prid $prid"

if [ -z "$force" ]; then
	k=$(sqlite3 "$db" "SELECT count(*) FROM yhat
	                   WHERE prid = $prid AND mid = $mid")
	[ "$k" = 0 ] ||
		die "model $mid has already scored problem $prid; pass -f to"\
		    "replace that"
fi

out=$("$scorer" ${verbose:+$verbose} ${force:+$force} -t "$db" -p "$prid" \
	-m "$mid")
n=$(printf '%s\n' "$out" | sed -n 's/^classified=//p')
isint "$n" || die "predict reported no count"

echo "$n"
