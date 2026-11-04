// JSON tree in an arena, and a writer that formats the way the data files are written.
#pragma once
#include "base.h"

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JsonType;

typedef struct Json {
  JsonType type;
  bool b;
  double num;
  const char *str;   // J_STR
  const char *key;   // member name inside an object
  struct Json *head; // first child (J_ARR, J_OBJ)
  struct Json *tail;
  struct Json *next;
  int count;
} Json;

// NULL on a syntax error; `err` (if given) receives "line N: what".
Json *json_parse(Arena *ar, const char *text, char *err, size_t errlen);
Json *json_get(const Json *obj, const char *key);
const char *json_str(const Json *obj, const char *key, const char *fallback);
double json_num(const Json *obj, const char *key, double fallback);
bool json_bool(const Json *obj, const char *key, bool fallback);
bool json_equal(const Json *a, const Json *b); // member order does not matter

Json *json_new(Arena *ar, JsonType t);
Json *json_add(Arena *ar, Json *parent, const char *key, Json *child);
Json *json_add_num(Arena *ar, Json *parent, const char *key, double v);
Json *json_add_str(Arena *ar, Json *parent, const char *key, const char *s);
Json *json_add_bool(Arena *ar, Json *parent, const char *key, bool v);
Json *json_clone(Arena *ar, const Json *j);

// Pretty printed with short arrays of numbers on one line. Caller frees with free().
char *json_write(const Json *j);
