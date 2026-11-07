// Small foundation shared by everything: integer names, growing arrays, an arena, vector maths.
#pragma once
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define PI 3.14159265358979323846f
#define TAU 6.28318530717958647692f

static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline float smoothstepf(float a, float b, float x) {
  float t = clampf((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
}

// ------------------------------------------------------------------ growing arrays
// T *a = NULL; arr_push(a, v); arr_len(a); arr_free(a). The header sits before the data.
typedef struct {
  int len, cap;
} ArrHdr;
#define arr__h(a) ((ArrHdr *)(a) - 1)
#define arr_len(a) ((a) ? arr__h(a)->len : 0)
#define arr_cap(a) ((a) ? arr__h(a)->cap : 0)
#define arr_free(a) ((void)((a) ? (free(arr__h(a)), (a) = NULL, 0) : 0))
#define arr_clear(a) ((void)((a) ? (arr__h(a)->len = 0) : 0))
#define arr_reserve(a, n) ((a) = arr__grow((a), sizeof(*(a)), (n)))
#define arr_push(a, ...) (arr_reserve((a), arr_len(a) + 1), (a)[arr__h(a)->len++] = (__VA_ARGS__))
#define arr_pop(a) ((void)--arr__h(a)->len)
#define arr_last(a) ((a)[arr__h(a)->len - 1])
#define arr_add(a, n) (arr_reserve((a), arr_len(a) + (n)), arr__h(a)->len += (n), (a) + arr__h(a)->len - (n))
#define arr_set_len(a, n) ((void)(arr_reserve((a), (n)), arr__h(a)->len = (n)))
#define arr_remove(a, i)                                                                     \
  (memmove((a) + (i), (a) + (i) + 1, sizeof(*(a)) * (size_t)(arr_len(a) - (i) - 1)), arr__h(a)->len--)
#define arr_insert(a, i, ...)                                                                \
  (arr_reserve((a), arr_len(a) + 1),                                                         \
   memmove((a) + (i) + 1, (a) + (i), sizeof(*(a)) * (size_t)(arr_len(a) - (i))), arr__h(a)->len++, \
   (a)[i] = (__VA_ARGS__))

static inline void *arr__grow(void *a, size_t item, int want) {
  int cap = arr_cap(a);
  if (a && want <= cap) return a;
  int next = cap ? cap * 2 : 8;
  if (next < want) next = want;
  ArrHdr *h = realloc(a ? arr__h(a) : NULL, sizeof(ArrHdr) + item * (size_t)next);
  if (!h) abort();
  if (!a) h->len = 0;
  h->cap = next;
  return h + 1;
}

// ------------------------------------------------------------------ arena
typedef struct ArenaBlock {
  struct ArenaBlock *next;
  size_t used, cap;
} ArenaBlock;
typedef struct {
  ArenaBlock *head;
} Arena;

static inline void *arena_alloc(Arena *ar, size_t n) {
  n = (n + 15) & ~(size_t)15;
  ArenaBlock *b = ar->head;
  if (!b || b->used + n > b->cap) {
    size_t cap = n > 60000 ? n : 60000;
    b = malloc(sizeof(ArenaBlock) + 16 + cap);
    if (!b) abort();
    b->next = ar->head;
    b->used = 0;
    b->cap = cap;
    ar->head = b;
  }
  void *p = (char *)(b + 1) + 16 + b->used;
  b->used += n;
  memset(p, 0, n);
  return p;
}
static inline char *arena_strn(Arena *ar, const char *s, size_t n) {
  char *d = arena_alloc(ar, n + 1);
  memcpy(d, s, n);
  d[n] = 0;
  return d;
}
static inline char *arena_str(Arena *ar, const char *s) { return s ? arena_strn(ar, s, strlen(s)) : NULL; }
static inline void arena_free(Arena *ar) {
  for (ArenaBlock *b = ar->head, *n; b; b = n) {
    n = b->next;
    free(b);
  }
  ar->head = NULL;
}

// ------------------------------------------------------------------ vectors
typedef struct {
  float x, y;
} Vec2;
typedef struct {
  float x, y, z;
} Vec3;
typedef struct {
  float x, y, z, w;
} Vec4;
typedef struct {
  float m[16];
} Mat4; // column major
typedef struct {
  Vec3 min, max;
} Box;

static inline Vec2 v2(float x, float y) { return (Vec2){x, y}; }
static inline Vec3 v3(float x, float y, float z) { return (Vec3){x, y, z}; }
static inline Vec3 v3add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 v3sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 v3scale(Vec3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static inline Vec3 v3mad(Vec3 a, Vec3 b, float k) { return v3(a.x + b.x * k, a.y + b.y * k, a.z + b.z * k); }
static inline float v3dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 v3cross(Vec3 a, Vec3 b) {
  return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3len(Vec3 a) { return sqrtf(v3dot(a, a)); }
static inline Vec3 v3norm(Vec3 a) {
  float l = v3len(a);
  return l > 0 ? v3scale(a, 1 / l) : a;
}
static inline Vec3 v3lerp(Vec3 a, Vec3 b, float t) {
  return v3(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t));
}
static inline float v3dist(Vec3 a, Vec3 b) { return v3len(v3sub(a, b)); }

static inline Mat4 m4identity(void) {
  return (Mat4){{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}
static inline Mat4 m4mul(Mat4 a, Mat4 b) {
  Mat4 r;
  for (int c = 0; c < 4; c++)
    for (int i = 0; i < 4; i++)
      r.m[c * 4 + i] = a.m[i] * b.m[c * 4] + a.m[4 + i] * b.m[c * 4 + 1] + a.m[8 + i] * b.m[c * 4 + 2] +
                       a.m[12 + i] * b.m[c * 4 + 3];
  return r;
}
static inline Mat4 m4translate(Vec3 t) {
  Mat4 r = m4identity();
  r.m[12] = t.x, r.m[13] = t.y, r.m[14] = t.z;
  return r;
}
static inline Mat4 m4scale(Vec3 s) {
  Mat4 r = m4identity();
  r.m[0] = s.x, r.m[5] = s.y, r.m[10] = s.z;
  return r;
}
static inline Mat4 m4rot_y(float a) {
  Mat4 r = m4identity();
  float c = cosf(a), s = sinf(a);
  r.m[0] = c, r.m[2] = -s, r.m[8] = s, r.m[10] = c;
  return r;
}
static inline Mat4 m4rot_x(float a) {
  Mat4 r = m4identity();
  float c = cosf(a), s = sinf(a);
  r.m[5] = c, r.m[6] = s, r.m[9] = -s, r.m[10] = c;
  return r;
}
static inline Mat4 m4rot_axis(Vec3 ax, float a) {
  float c = cosf(a), s = sinf(a), t = 1 - c;
  Mat4 r = m4identity();
  r.m[0] = t * ax.x * ax.x + c, r.m[4] = t * ax.x * ax.y - s * ax.z, r.m[8] = t * ax.x * ax.z + s * ax.y;
  r.m[1] = t * ax.x * ax.y + s * ax.z, r.m[5] = t * ax.y * ax.y + c, r.m[9] = t * ax.y * ax.z - s * ax.x;
  r.m[2] = t * ax.x * ax.z - s * ax.y, r.m[6] = t * ax.y * ax.z + s * ax.x, r.m[10] = t * ax.z * ax.z + c;
  return r;
}
static inline Vec3 m4point(Mat4 m, Vec3 p) {
  return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}
static inline Vec3 m4dir(Mat4 m, Vec3 p) {
  return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z, m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z,
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z);
}
static inline Vec4 m4vec4(Mat4 m, Vec3 p) {
  return (Vec4){m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
                m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
                m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14],
                m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15]};
}
static inline Mat4 m4perspective(float fovy_rad, float aspect, float near, float far) {
  float f = 1 / tanf(fovy_rad / 2);
  Mat4 r = {{0}};
  r.m[0] = f / aspect, r.m[5] = f, r.m[10] = (far + near) / (near - far), r.m[11] = -1;
  r.m[14] = 2 * far * near / (near - far);
  return r;
}
static inline Mat4 m4lookat(Vec3 eye, Vec3 at, Vec3 up) {
  Vec3 f = v3norm(v3sub(at, eye)), s = v3norm(v3cross(f, up)), u = v3cross(s, f);
  return (Mat4){{s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0, -v3dot(s, eye), -v3dot(u, eye),
                 v3dot(f, eye), 1}};
}
Mat4 m4invert(Mat4 a); // model.c

static inline Box box_empty(void) { return (Box){v3(INFINITY, INFINITY, INFINITY), v3(-INFINITY, -INFINITY, -INFINITY)}; }
static inline bool box_is_empty(Box b) { return b.max.x < b.min.x; }
static inline void box_add(Box *b, Vec3 p) {
  b->min = v3(minf(b->min.x, p.x), minf(b->min.y, p.y), minf(b->min.z, p.z));
  b->max = v3(maxf(b->max.x, p.x), maxf(b->max.y, p.y), maxf(b->max.z, p.z));
}
static inline void box_union(Box *b, Box o) {
  if (box_is_empty(o)) return;
  box_add(b, o.min);
  box_add(b, o.max);
}
static inline Vec3 box_center(Box b) { return v3scale(v3add(b.min, b.max), 0.5f); }
static inline Vec3 box_size(Box b) { return v3sub(b.max, b.min); }
static inline float box_radius(Box b) { return v3len(box_size(b)) * 0.5f; }
static inline Box box_shift(Box b, Vec3 d) {
  if (box_is_empty(b)) return b;
  return (Box){v3add(b.min, d), v3add(b.max, d)};
}

// The same seeded generator the data's layouts were designed with: a layout never changes.
typedef struct {
  u32 a;
} Rng;
static inline Rng rng_new(u32 seed) { return (Rng){seed}; }
static inline double rng_next(Rng *r) {
  r->a += 0x6d2b79f5u;
  u32 t = r->a;
  t = (t ^ (t >> 15)) * (t | 1);
  t ^= t + (t ^ (t >> 7)) * (t | 61);
  return (double)(t ^ (t >> 14)) / 4294967296.0;
}

// Colour: hex "#rrggbb" to sRGB 0..1 and linear.
static inline float srgb_to_linear(float c) {
  return c < 0.04045f ? c * 0.0773993808f : powf(c * 0.9478672986f + 0.0521327014f, 2.4f);
}
static inline Vec3 hex_srgb(const char *s) {
  if (!s) return v3(1, 1, 1);
  if (*s == '#') s++;
  u32 n = (u32)strtoul(s, NULL, 16);
  return v3(((n >> 16) & 255) / 255.0f, ((n >> 8) & 255) / 255.0f, (n & 255) / 255.0f);
}
static inline Vec3 rgb_linear(Vec3 c) { return v3(srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z)); }
static inline Vec3 hex_linear(const char *s) { return rgb_linear(hex_srgb(s)); }

// Files: whole-file read; NUL terminated. Web builds serve these from the page's embedded data.
char *file_read(const char *path, size_t *len);
bool file_write(const char *path, const void *data, size_t len);
// `rel` resolved against the folder of `from`; collapses "x/../". Result lives until the arena dies.
char *path_join(Arena *ar, const char *from, const char *rel);
bool path_same(const char *a, const char *b); // the same file, however it was reached
// `to` as seen from the folder of `from`, for the links inside files.
void path_rel(const char *from, const char *to, char *out, size_t n);
