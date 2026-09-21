/*
** Helpers shared by the whole program.
*/

#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

extern const char *argv0;

void die(const char *fmt, ...);
void *ecalloc(size_t num, size_t size);

#endif /* UTIL_H */
