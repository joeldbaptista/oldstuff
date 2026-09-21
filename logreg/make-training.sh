#!/bin/sh
#
# make-training.sh - create an empty SVM training database.
#
# usage: make-training.sh [-f] -o training.sqlite3
#
# The schema comes from sql/ddl.sql, next to this script.

set -eu

argv0=${0##*/}
here=$(cd "$(dirname "$0")" && pwd)
ddl=$here/sql/ddl.sql

usage() {
	echo "usage: $argv0 [-f] -o training.sqlite3" >&2
	exit 1
}

die() {
	echo "$argv0: $*" >&2
	exit 1
}

out=
force=0
while [ $# -gt 0 ]; do
	case $1 in
	-f)
		force=1
		;;
	-o)
		[ $# -ge 2 ] || usage
		out=$2
		shift
		;;
	*)
		usage
		;;
	esac
	shift
done

[ -n "$out" ] || usage
[ -f "$ddl" ] || die "cannot find $ddl"

if [ -e "$out" ]; then
	[ "$force" -eq 1 ] || die "$out exists; pass -f to overwrite it"
	rm -f "$out"
fi

if ! sqlite3 "$out" < "$ddl"; then
	rm -f "$out"
	die "cannot create $out"
fi
