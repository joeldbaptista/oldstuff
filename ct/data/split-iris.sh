#!/bin/sh
#
# split-iris.sh - cut iris.csv into a training part and a test part.
#
# usage: data/split-iris.sh
#
# The split is deterministic and stratified: within each class, in file
# order, every fourth data point goes to the test part.  So the class
# balance of both parts stays close to that of the whole, and the files
# can be regenerated at any time.
#
# iris.csv holds a header and one data point per row, the four variables
# first and the class label last.  The files written here carry the
# label first and a weight of 1.0 second, which is the layout
# add-training-data.sh reads.
#
# It writes four files next to iris.csv:
#
#   iris-train.csv           labelled, for add-training-data.sh
#   iris-train-metadata.csv  the m and n of that part
#   iris-test.csv            the same points without their label, for
#                            add-problem.sh
#   iris-test-labels.csv     dpid and the true class, for checking what
#                            apply-ct.sh produced

set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$here/iris.csv
[ -r "$src" ] || { echo "cannot read $src" >&2; exit 1; }

awk -F'[|]' -v d="$here" '
	function trim(s) {
		gsub(/^[ \t\r]+|[ \t\r]+$/, "", s)
		return s
	}
	BEGIN {
		tr = d "/iris-train.csv"
		te = d "/iris-test.csv"
		lb = d "/iris-test-labels.csv"
		printf "dpid|y\n" > lb
	}
	NR == 1 { next }
	/^[ \t\r]*$/ { next }
	{
		lab = trim($NF)
		seen[lab]++
		vars = trim($1)
		for (i = 2; i < NF; i++)
			vars = vars "|" trim($i)

		if (seen[lab] % 4 == 0) {
			print vars > te
			printf "%d|%s\n", ntest++, lab > lb
		} else {
			printf "%s|1.0|%s\n", lab, vars > tr
			ntrain++
		}
		n = NF - 1
	}
	END {
		printf "m|n|comments\n%d|%d|iris, training part\n",
		    ntrain, n > (d "/iris-train-metadata.csv")
		printf "train %d, test %d, variables %d\n", ntrain, ntest, n
	}
' "$src"
