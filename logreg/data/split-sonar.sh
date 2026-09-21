#!/bin/sh
#
# split-sonar.sh - cut sonar.csv into a training part and a test part.
#
# usage: data/split-sonar.sh
#
# The split is deterministic and stratified: within each class, in file
# order, every fourth data point goes to the test part.  So the class
# balance of both parts stays close to that of the whole, and the files
# can be regenerated at any time.
#
# It writes four files next to sonar.csv:
#
#   sonar-train.csv           labelled and weighted, for
#                             add-training-data.sh
#   sonar-train-metadata.csv  the m and n of that part
#   sonar-test.csv            the same points without label or weight,
#                             for add-problem.sh
#   sonar-test-labels.csv     dpid and the true label, for checking what
#                             apply-svm.sh produced

set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$here/sonar.csv
[ -r "$src" ] || { echo "cannot read $src" >&2; exit 1; }

awk -F'[|]' -v d="$here" '
	function trim(s) {
		gsub(/^[ \t\r]+|[ \t\r]+$/, "", s)
		return s
	}
	BEGIN {
		tr = d "/sonar-train.csv"
		te = d "/sonar-test.csv"
		lb = d "/sonar-test-labels.csv"
		printf "dpid|y\n" > lb
	}
	/^[ \t\r]*$/ { next }
	{
		lab = trim($1)
		seen[lab]++
		row = trim($1)
		for (i = 2; i <= NF; i++)
			row = row "|" trim($i)
		vars = trim($3)
		for (i = 4; i <= NF; i++)
			vars = vars "|" trim($i)

		if (seen[lab] % 4 == 0) {
			print vars > te
			printf "%d|%s\n", ntest++, lab > lb
		} else {
			print row > tr
			ntrain++
		}
		n = NF - 2
	}
	END {
		printf "m|n|comments\n%d|%d|sonar, training part\n",
		    ntrain, n > (d "/sonar-train-metadata.csv")
		printf "train %d, test %d, variables %d\n", ntrain, ntest, n
	}
' "$src"
