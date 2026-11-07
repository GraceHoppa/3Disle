// The little C library the web build runs on. Number text goes through the browser, which gets it
// exactly right; memory comes from the wasm page allocator in size classes.
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char __heap_base;
// Imported from glue.js.
void js_log(const char *s, int len);
void js_abort(void);
int js_format(double v, int conv, int prec, char *out, int cap);
double js_strtod(const char *s, int *used);

// ------------------------------------------------------------------ memory
// Blocks are powers of two from 32 bytes up, with their class in an 16-byte header, so `free` needs no
// search and a freed block is reused by the next request of its size.
#define MIN_SHIFT 5
#define CLASSES 28
typedef struct Free {
  struct Free *next;
} Free;
typedef struct {
  uint32_t cls;
  uint32_t pad[3];
} Header;
static Free *g_free[CLASSES];
static uintptr_t g_top;
static uintptr_t g_end;

static void *grab(size_t n) {
  if (!g_top) {
    g_top = ((uintptr_t)&__heap_base + 15) & ~(uintptr_t)15;
    g_end = __builtin_wasm_memory_size(0) * 65536u;
  }
  while (g_top + n > g_end) {
    size_t pages = (n + 65535) / 65536;
    pages = pages < 256 ? 256 : pages;
    if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) return NULL;
    g_end += pages * 65536u;
  }
  void *p = (void *)g_top;
  g_top += n;
  return p;
}

void *malloc(size_t n) {
  size_t need = n + sizeof(Header);
  int c = 0;
  while (((size_t)1 << (c + MIN_SHIFT)) < need) c++;
  if (c >= CLASSES) return NULL;
  Header *h;
  if (g_free[c]) {
    h = (Header *)g_free[c];
    g_free[c] = g_free[c]->next;
  } else {
    h = grab((size_t)1 << (c + MIN_SHIFT));
    if (!h) return NULL;
  }
  h->cls = (uint32_t)c;
  return h + 1;
}

void free(void *p) {
  if (!p) return;
  Header *h = (Header *)p - 1;
  int c = (int)h->cls;
  Free *f = (Free *)h;
  f->next = g_free[c];
  g_free[c] = f;
}

void *calloc(size_t a, size_t b) {
  size_t n = a * b;
  void *p = malloc(n);
  if (p) memset(p, 0, n);
  return p;
}

void *realloc(void *p, size_t n) {
  if (!p) return malloc(n);
  Header *h = (Header *)p - 1;
  size_t have = ((size_t)1 << (h->cls + MIN_SHIFT)) - sizeof(Header);
  if (n <= have) return p;
  void *q = malloc(n);
  if (!q) return NULL;
  memcpy(q, p, have);
  free(p);
  return q;
}

// ------------------------------------------------------------------ strings
void *memcpy(void *d, const void *s, size_t n) {
  __builtin_memcpy(d, s, n);
  return d;
}
void *memmove(void *d, const void *s, size_t n) {
  __builtin_memmove(d, s, n);
  return d;
}
void *memset(void *d, int c, size_t n) {
  __builtin_memset(d, c, n);
  return d;
}
int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (size_t i = 0; i < n; i++)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}
size_t strlen(const char *s) {
  const char *e = s;
  while (*e) e++;
  return (size_t)(e - s);
}
int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) a++, b++;
  return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    if (!a[i]) return 0;
  }
  return 0;
}
char *strchr(const char *s, int c) {
  for (;; s++) {
    if (*s == (char)c) return (char *)s;
    if (!*s) return NULL;
  }
}
char *strrchr(const char *s, int c) {
  const char *hit = NULL;
  for (;; s++) {
    if (*s == (char)c) hit = s;
    if (!*s) return (char *)hit;
  }
}
char *strstr(const char *h, const char *n) {
  size_t k = strlen(n);
  for (; *h; h++)
    if (!strncmp(h, n, k)) return (char *)h;
  return k ? NULL : (char *)h;
}
char *strpbrk(const char *s, const char *any) {
  for (; *s; s++)
    if (strchr(any, *s)) return (char *)s;
  return NULL;
}
void *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = s;
  for (size_t i = 0; i < n; i++)
    if (p[i] == (unsigned char)c) return (void *)(p + i);
  return NULL;
}
char *strcpy(char *d, const char *s) {
  char *r = d;
  while ((*d++ = *s++)) {}
  return r;
}
char *strncat(char *d, const char *s, size_t n) {
  char *e = d + strlen(d);
  while (n-- && *s) *e++ = *s++;
  *e = 0;
  return d;
}
char *strerror(int e) { return (char *)"error"; }

// ------------------------------------------------------------------ numbers from text
double strtod(const char *s, char **end) {
  int used = 0;
  double v = js_strtod(s, &used);
  if (end) *end = (char *)s + used;
  return v;
}
long strtol(const char *s, char **end, int base) {
  const char *p = s;
  while (*p == ' ' || *p == '\t' || *p == '\n') p++;
  bool neg = *p == '-';
  if (*p == '-' || *p == '+') p++;
  if ((base == 16 || base == 0) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2, base = 16;
  if (!base) base = 10;
  long v = 0;
  for (;; p++) {
    int d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'z' ? *p - 'a' + 10 : *p >= 'A' && *p <= 'Z' ? *p - 'A' + 10 : 99;
    if (d >= base) break;
    v = v * base + d;
  }
  if (end) *end = (char *)p;
  return neg ? -v : v;
}
unsigned long strtoul(const char *s, char **end, int base) {
  const char *p = s;
  while (*p == ' ' || *p == '\t' || *p == '\n') p++;
  if ((base == 16 || base == 0) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2, base = 16;
  if (!base) base = 10;
  unsigned long v = 0;
  for (;; p++) {
    int d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'z' ? *p - 'a' + 10 : *p >= 'A' && *p <= 'Z' ? *p - 'A' + 10 : 99;
    if (d >= base) break;
    v = v * (unsigned long)base + (unsigned long)d;
  }
  if (end) *end = (char *)p;
  return v;
}
double atof(const char *s) { return strtod(s, NULL); }
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }

// ------------------------------------------------------------------ sorting: stable, as the desktop's
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
  if (n < 2) return;
  char *a = base, *tmp = malloc(n * size);
  for (size_t width = 1; width < n; width *= 2) {
    for (size_t lo = 0; lo < n; lo += 2 * width) {
      size_t mid = lo + width < n ? lo + width : n, hi = lo + 2 * width < n ? lo + 2 * width : n, i = lo, j = mid, k = lo;
      while (i < mid && j < hi) {
        if (cmp(a + j * size, a + i * size) < 0) memcpy(tmp + k++ * size, a + j++ * size, size);
        else memcpy(tmp + k++ * size, a + i++ * size, size);
      }
      while (i < mid) memcpy(tmp + k++ * size, a + i++ * size, size);
      while (j < hi) memcpy(tmp + k++ * size, a + j++ * size, size);
    }
    memcpy(a, tmp, n * size);
  }
  free(tmp);
}

_Noreturn void abort(void) {
  js_abort();
  __builtin_trap();
}

// ------------------------------------------------------------------ formatted text
typedef struct {
  char *out;
  size_t cap, at;
} Sink;

static void put(Sink *s, char c) {
  if (s->at + 1 < s->cap) s->out[s->at] = c;
  s->at++;
}

static void pad_out(Sink *s, const char *text, int len, int width, bool left, char fill) {
  // Zeros go after the sign.
  if (fill == '0' && !left && len > 0 && (text[0] == '-' || text[0] == '+') && width > len) {
    put(s, text[0]);
    text++, len--, width--;
  }
  if (!left)
    for (int i = len; i < width; i++) put(s, fill);
  for (int i = 0; i < len; i++) put(s, text[i]);
  if (left)
    for (int i = len; i < width; i++) put(s, ' ');
}

int vsnprintf(char *out, size_t n, const char *f, va_list ap) {
  Sink s = {out, n, 0};
  char num[128];
  for (; *f; f++) {
    if (*f != '%') {
      put(&s, *f);
      continue;
    }
    f++;
    bool left = false, plus = false, space = false;
    char fill = ' ';
    for (;; f++) {
      if (*f == '-') left = true;
      else if (*f == '0') fill = '0';
      else if (*f == '+') plus = true;
      else if (*f == ' ') space = true;
      else if (*f != '#') break;
    }
    int width = 0, prec = -1;
    if (*f == '*') {
      width = va_arg(ap, int);
      if (width < 0) left = true, width = -width;
      f++;
    } else {
      while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
    }
    if (*f == '.') {
      f++;
      prec = 0;
      if (*f == '*') {
        prec = va_arg(ap, int);
        f++;
      } else {
        while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
      }
    }
    int longs = 0;
    while (*f == 'l' || *f == 'z' || *f == 'h' || *f == 't') {
      if (*f == 'l' || *f == 'z' || *f == 't') longs++;
      f++;
    }
    switch (*f) {
    case '%': put(&s, '%'); break;
    case 'c': {
      char c = (char)va_arg(ap, int);
      pad_out(&s, &c, 1, width, left, ' ');
      break;
    }
    case 's': {
      const char *t = va_arg(ap, const char *);
      if (!t) t = "(null)";
      int len = 0;
      while (t[len] && (prec < 0 || len < prec)) len++;
      pad_out(&s, t, len, width, left, ' ');
      break;
    }
    case 'd':
    case 'i':
    case 'u':
    case 'x':
    case 'X': {
      long long v;
      if (*f == 'd' || *f == 'i') v = longs >= 2 ? va_arg(ap, long long) : longs ? va_arg(ap, long) : va_arg(ap, int);
      else v = longs >= 2 ? (long long)va_arg(ap, unsigned long long) : longs ? (long long)va_arg(ap, unsigned long) : (long long)va_arg(ap, unsigned);
      bool neg = (*f == 'd' || *f == 'i') && v < 0;
      unsigned long long u = neg ? (unsigned long long)-v : (unsigned long long)v;
      int base = *f == 'x' || *f == 'X' ? 16 : 10, k = (int)sizeof num;
      const char *digs = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
      do {
        num[--k] = digs[u % (unsigned)base];
        u /= (unsigned)base;
      } while (u && k > 2);
      if (neg) num[--k] = '-';
      else if (plus) num[--k] = '+';
      else if (space) num[--k] = ' ';
      pad_out(&s, num + k, (int)sizeof num - k, width, left, fill);
      break;
    }
    case 'f':
    case 'e':
    case 'g':
    case 'F':
    case 'E':
    case 'G': {
      double v = va_arg(ap, double);
      int len = js_format(v, *f | 32, prec < 0 ? 6 : prec, num, (int)sizeof num);
      char sign[129];
      int at = 0;
      if (plus && v >= 0) sign[at++] = '+';
      else if (space && v >= 0) sign[at++] = ' ';
      memcpy(sign + at, num, (size_t)len);
      pad_out(&s, sign, len + at, width, left, fill);
      break;
    }
    default: put(&s, *f); break;
    }
  }
  if (n) out[s.at < n ? s.at : n - 1] = 0;
  return (int)s.at;
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(out, n, fmt, ap);
  va_end(ap);
  return r;
}

static char g_line[2048];
int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(g_line, sizeof g_line, fmt, ap);
  va_end(ap);
  js_log(g_line, (int)strlen(g_line));
  return r;
}
int fprintf(FILE *f, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(g_line, sizeof g_line, fmt, ap);
  va_end(ap);
  js_log(g_line, (int)strlen(g_line));
  return r;
}
int fflush(FILE *f) { return 0; }
