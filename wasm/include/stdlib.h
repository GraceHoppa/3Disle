#pragma once
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t a, size_t b);
void *realloc(void *p, size_t n);
void free(void *p);
double strtod(const char *s, char **end);
unsigned long strtoul(const char *s, char **end, int base);
long strtol(const char *s, char **end, int base);
double atof(const char *s);
int atoi(const char *s);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
_Noreturn void abort(void);
