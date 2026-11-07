// What the engine needs of stdio on the web: formatted text, and messages to the browser's console.
#pragma once
#include <stdarg.h>
#include <stddef.h>
typedef struct FILE FILE;
#define stderr ((FILE *)2)
#define stdout ((FILE *)1)
int snprintf(char *out, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *out, size_t n, const char *fmt, va_list ap);
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int fflush(FILE *f);
