/*
** Reading the comma-delimited data file.  See csv.h.
*/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csv.h"
#include "util.h"

static char *trim(char *s);
static int readline_(FILE *fp, char **buf, size_t *max);
static int split(char *s, char ***fld, int *maxfld);
static int numeric(const char *s, double *out);

static char *
trim(char *s)
{
	char *e;

	while (isspace((unsigned char)*s))
		s++;
	if (!*s)
		return s;
	e = s + strlen(s) - 1;
	while (e > s && isspace((unsigned char)*e))
		e--;
	e[1] = '\0';
	return s;
}

/*
** Read one line without its newline.  Return 1 on a line and 0 at end
** of file.
*/
static int
readline_(FILE *fp, char **buf, size_t *max)
{
	size_t n;
	int ch;

	n = 0;
	for (;;) {
		ch = getc(fp);
		if (ch == EOF || ch == '\n')
			break;
		if (n + 2 > *max) {
			*max = *max ? *max * 2 : 256;
			*buf = erealloc(*buf, *max);
		}
		(*buf)[n++] = (char)ch;
	}
	if (ch == EOF && n == 0)
		return 0;
	if (n + 1 > *max) {
		*max = *max ? *max * 2 : 256;
		*buf = erealloc(*buf, *max);
	}
	(*buf)[n] = '\0';
	return 1;
}

/*
** Cut s apart on commas and return the number of fields.
*/
static int
split(char *s, char ***fld, int *maxfld)
{
	char *p, *q;
	int num;

	num = 0;
	p = s;
	for (;;) {
		q = strchr(p, ',');
		if (q)
			*q = '\0';
		if (num >= *maxfld) {
			*maxfld = *maxfld ? *maxfld * 2 : 16;
			*fld = erealloc(*fld, (size_t)*maxfld * sizeof(**fld));
		}
		(*fld)[num++] = trim(p);
		if (!q)
			break;
		p = q + 1;
	}
	return num;
}

static int
numeric(const char *s, double *out)
{
	char *end;

	if (!*s)
		return 0;
	errno = 0;
	*out = strtod(s, &end);
	return *end == '\0' && errno != ERANGE;
}

void
csvread(Data *d, const char *path)
{
	FILE *fp;
	char *buf, **fld;
	size_t max;
	double v;
	long line;
	int num, maxfld, i, cap;

	memset(d, 0, sizeof(*d));
	buf = 0;
	fld = 0;
	max = 0;
	maxfld = 0;
	cap = 0;
	line = 0;
	if (!(fp = fopen(path, "r")))
		die("cannot open %s: %s", path, strerror(errno));

	while (readline_(fp, &buf, &max)) {
		line++;
		num = split(buf, &fld, &maxfld);
		if (num == 1 && !*fld[0])
			continue; /* a blank line */
		if (!d->dim) {
			/* A first line that is not numeric is a header. */
			for (i = 0; i < num; i++)
				if (!numeric(fld[i], &v))
					break;
			if (i < num && line == 1)
				continue;
			d->dim = num;
		}
		if (num != d->dim)
			die("%s:%ld: expected %d fields, found %d", path, line,
			    d->dim, num);
		if (d->n >= cap) {
			cap = cap ? cap * 2 : 256;
			d->val = erealloc(d->val, (size_t)cap * d->dim *
			                                  sizeof(*d->val));
		}
		for (i = 0; i < num; i++) {
			if (!numeric(fld[i], &v))
				die("%s:%ld: field %d is not a number: %s",
				    path, line, i + 1, fld[i]);
			d->val[(size_t)d->n * d->dim + i] = v;
		}
		d->n++;
	}
	fclose(fp);
	free(buf);
	free(fld);
	if (d->n < 1)
		die("%s holds no data points", path);
}
