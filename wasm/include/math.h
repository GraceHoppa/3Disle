// Wasm does the simple ones in hardware; the rest come from the browser's Math.
#pragma once
#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define M_PI 3.14159265358979323846
#define HUGE_VAL (__builtin_inf())
static inline double fabs(double x) { return __builtin_fabs(x); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline float sqrtf(float x) { return __builtin_sqrtf(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline double ceil(double x) { return __builtin_ceil(x); }
static inline float ceilf(float x) { return __builtin_ceilf(x); }
static inline double trunc(double x) { return __builtin_trunc(x); }
static inline float truncf(float x) { return __builtin_truncf(x); }
static inline double copysign(double a, double b) { return __builtin_copysign(a, b); }
static inline double fmin(double a, double b) { return a != a ? b : b != b ? a : (a < b ? a : b); }
static inline double fmax(double a, double b) { return a != a ? b : b != b ? a : (a > b ? a : b); }
static inline double round(double x) { return x < 0 ? -__builtin_floor(-x + 0.5) : __builtin_floor(x + 0.5); }
static inline float roundf(float x) { return x < 0 ? -__builtin_floorf(-x + 0.5f) : __builtin_floorf(x + 0.5f); }
static inline long lround(double x) { return (long)round(x); }
static inline long lroundf(float x) { return (long)roundf(x); }
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double acos(double x);
double asin(double x);
double pow(double a, double b);
double exp(double x);
double log(double x);
double log2(double x);
double log10(double x);
double fmod(double a, double b);
static inline double hypot(double a, double b) { return __builtin_sqrt(a * a + b * b); }
static inline float hypotf(float a, float b) { return __builtin_sqrtf(a * a + b * b); }
static inline float sinf(float x) { return (float)sin(x); }
static inline float cosf(float x) { return (float)cos(x); }
static inline float tanf(float x) { return (float)tan(x); }
static inline float atanf(float x) { return (float)atan(x); }
static inline float atan2f(float y, float x) { return (float)atan2(y, x); }
static inline float acosf(float x) { return (float)acos(x); }
static inline float asinf(float x) { return (float)asin(x); }
static inline float powf(float a, float b) { return (float)pow(a, b); }
static inline float expf(float x) { return (float)exp(x); }
static inline float logf(float x) { return (float)log(x); }
static inline float log2f(float x) { return (float)log2(x); }
static inline float log10f(float x) { return (float)log10(x); }
static inline float fmodf(float a, float b) { return (float)fmod(a, b); }
