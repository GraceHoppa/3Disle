// Native files and the one stb_truetype implementation.
#include "base.h"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC_NOT
#include "stb_truetype.h"

#ifndef __wasm__
char *file_read(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0) {
    fclose(f);
    return NULL;
  }
  char *d = malloc((size_t)n + 1);
  size_t got = d ? fread(d, 1, (size_t)n, f) : 0;
  fclose(f);
  if (!d || got != (size_t)n) {
    free(d);
    return NULL;
  }
  d[n] = 0;
  if (len) *len = (size_t)n;
  return d;
}

bool file_write(const char *path, const void *data, size_t len) {
  // Write beside it, then rename: a crash never leaves half a file where the work was.
  char tmp[4096];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, len, f) == len;
  ok = !fclose(f) && ok;
  if (ok) ok = !rename(tmp, path);
  if (!ok) remove(tmp);
  return ok;
}
#endif

char *path_join(Arena *ar, const char *from, const char *rel) {
  if (rel[0] == '/') return arena_str(ar, rel);
  const char *slash = strrchr(from, '/');
  size_t dir = slash ? (size_t)(slash - from) + 1 : 0;
  size_t cap = dir + strlen(rel) + 1;
  char *joined = arena_alloc(ar, cap);
  memcpy(joined, from, dir);
  strcpy(joined + dir, rel);
  // Collapse "x/../" and "./".
  char *out = arena_alloc(ar, cap);
  size_t o = 0;
  for (char *p = joined; *p;) {
    char *e = strchr(p, '/');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n == 1 && p[0] == '.') {
      // skip
    } else if (n == 2 && p[0] == '.' && p[1] == '.' && o > 0 && !(o >= 3 && !strncmp(out + o - 3, "../", 3))) {
      o--; // the slash
      while (o > 0 && out[o - 1] != '/') o--;
    } else {
      memcpy(out + o, p, n);
      o += n;
      if (e) out[o++] = '/';
    }
    if (!e) break;
    p = e + 1;
  }
  out[o] = 0;
  return out;
}

#ifdef __wasm__
bool path_same(const char *a, const char *b) { return !strcmp(a, b); }
void path_rel(const char *from, const char *to, char *out, size_t n) { snprintf(out, n, "%s", to); }
#else
void path_rel(const char *from, const char *to, char *out, size_t n) {
  char ra[4096], rb[4096], dir[4096];
  snprintf(dir, sizeof dir, "%s", from);
  char *slash = strrchr(dir, '/');
  if (slash) *slash = 0;
  else snprintf(dir, sizeof dir, ".");
  if (!realpath(dir, ra) || !realpath(to, rb)) {
    snprintf(out, n, "%s", to);
    return;
  }
  size_t common = 0, i = 0;
  while (ra[i] && rb[i] && ra[i] == rb[i]) {
    if (ra[i] == '/') common = i + 1;
    i++;
  }
  if (!ra[i] && rb[i] == '/') common = i + 1;
  size_t at = 0;
  out[0] = 0;
  if (common <= strlen(ra))
    for (const char *p = ra + common; *p && at + 4 < n;) {
      at += (size_t)snprintf(out + at, n - at, "../");
      while (*p && *p != '/') p++;
      if (*p) p++;
    }
  snprintf(out + at, n - at, "%s", rb + common);
}
bool path_same(const char *a, const char *b) {
  if (!strcmp(a, b)) return true;
  char ra[4096], rb[4096];
  if (!realpath(a, ra) || !realpath(b, rb)) return false;
  return !strcmp(ra, rb);
}
#endif
