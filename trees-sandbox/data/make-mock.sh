#!/bin/sh
#
# make-mock.sh - generate the mock dataset.
#
# usage: data/make-mock.sh
#
# It writes 1000 points over 10 variables, every value in [0, 1], in five
# groups chosen so that the tree has something to find:
#
#   A  300 points around 0.30, spread 0.04
#   B  300 points around 0.36, spread 0.04   close to A, the clouds overlap
#   C  190 points around 0.80, spread 0.03   isolated
#   D  190 points around 0.15 in the first five variables and 0.85 in the
#                             last five, spread 0.03   isolated
#   E   20 points drawn uniformly over the whole cube, so each one sits on
#                             its own, far from everything
#
# Two files come out: mock.csv, which the tree reads, and mock-truth.csv,
# which names the group of each point so that the result can be checked.
#
# The generator is a Lehmer sequence written out in full rather than
# awk's rand(), so the files are the same on any awk.

set -eu

here=$(cd "$(dirname "$0")" && pwd)

awk -v d="$here" '
	function uni(   ) {
		seed = (16807 * seed) % 2147483647
		return seed / 2147483647.0
	}
	function nrm(   u1, u2) {
		u1 = uni()
		if (u1 < 1e-12)
			u1 = 1e-12
		u2 = uni()
		return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2)
	}
	function clamp(v) {
		if (v < 0.0)
			return 0.0
		if (v > 1.0)
			return 1.0
		return v
	}
	function emit(group, centre, sd,   j, row, v) {
		row = ""
		for (j = 1; j <= N; j++) {
			if (sd > 0.0)
				v = clamp(centre[j] + sd * nrm())
			else
				v = uni()
			row = row (j > 1 ? "," : "") sprintf("%.6f", v)
		}
		print row > out
		printf "%d,%s\n", id++, group > truth
	}
	BEGIN {
		N = 10
		seed = 20260920
		out = d "/mock.csv"
		truth = d "/mock-truth.csv"

		hdr = ""
		for (j = 1; j <= N; j++)
			hdr = hdr (j > 1 ? "," : "") "v" j
		print hdr > out
		print "point,group" > truth
		id = 0

		for (j = 1; j <= N; j++) { a[j] = 0.30; b[j] = 0.36 }
		for (j = 1; j <= N; j++) c[j] = 0.80
		for (j = 1; j <= N; j++) e[j] = 0.0
		for (j = 1; j <= 5; j++) f[j] = 0.15
		for (j = 6; j <= N; j++) f[j] = 0.85

		for (i = 0; i < 300; i++) emit("A", a, 0.04)
		for (i = 0; i < 300; i++) emit("B", b, 0.04)
		for (i = 0; i < 190; i++) emit("C", c, 0.03)
		for (i = 0; i < 190; i++) emit("D", f, 0.03)
		for (i = 0; i <  20; i++) emit("E", e, 0.0)

		printf "wrote %d points over %d variables\n", id, N
	}
' /dev/null
