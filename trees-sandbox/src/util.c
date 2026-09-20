/*
** Helpers shared by the whole program.  See util.h.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

const char *argv0;

void
die(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", argv0 ? argv0 : "itree");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void *
ecalloc(size_t num, size_t size)
{
	void *p;

	if (!(p = calloc(num, size)))
		die("out of memory");
	return p;
}

void *
erealloc(void *p, size_t size)
{
	void *q;

	if (!(q = realloc(p, size)))
		die("out of memory");
	return q;
}
