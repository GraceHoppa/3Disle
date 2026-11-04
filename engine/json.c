#include "json.h"

typedef struct {
  const char *s, *start;
  Arena *ar;
  char *err;
  size_t errlen;
  bool failed;
} Parser;

static void fail(Parser *p, const char *what) {
  if (p->failed) return;
  p->failed = true;
  if (!p->err) return;
  int line = 1;
  for (const char *c = p->start; c < p->s && *c; c++)
    if (*c == '\n') line++;
  snprintf(p->err, p->errlen, "line %d: %s", line, what);
}

static void skip_ws(Parser *p) {
  while (*p->s == ' ' || *p->s == '\n' || *p->s == '\t' || *p->s == '\r') p->s++;
}

static int hex4(const char *s) {
  int v = 0;
  for (int i = 0; i < 4; i++) {
    char c = s[i];
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) return -1;
    v = v * 16 + d;
  }
  return v;
}

static int put_utf8(char *d, u32 c) {
  if (c < 0x80) return d[0] = (char)c, 1;
  if (c < 0x800) return d[0] = (char)(0xC0 | c >> 6), d[1] = (char)(0x80 | (c & 63)), 2;
  if (c < 0x10000)
    return d[0] = (char)(0xE0 | c >> 12), d[1] = (char)(0x80 | ((c >> 6) & 63)), d[2] = (char)(0x80 | (c & 63)), 3;
  d[0] = (char)(0xF0 | c >> 18), d[1] = (char)(0x80 | ((c >> 12) & 63)), d[2] = (char)(0x80 | ((c >> 6) & 63));
  d[3] = (char)(0x80 | (c & 63));
  return 4;
}

static char *parse_string(Parser *p) {
  p->s++; // opening quote
  const char *e = p->s;
  while (*e && *e != '"') e += (*e == '\\' && e[1]) ? 2 : 1;
  if (*e != '"') {
    fail(p, "unterminated string");
    return NULL;
  }
  char *out = arena_alloc(p->ar, (size_t)(e - p->s) + 1), *d = out;
  for (const char *c = p->s; c < e; c++) {
    if (*c != '\\') {
      *d++ = *c;
      continue;
    }
    c++;
    switch (*c) {
    case 'n': *d++ = '\n'; break;
    case 't': *d++ = '\t'; break;
    case 'r': *d++ = '\r'; break;
    case 'b': *d++ = '\b'; break;
    case 'f': *d++ = '\f'; break;
    case 'u': {
      int u = hex4(c + 1);
      if (u < 0) {
        fail(p, "bad \\u escape");
        return NULL;
      }
      c += 4;
      u32 cp = (u32)u;
      if (u >= 0xD800 && u < 0xDC00 && c[1] == '\\' && c[2] == 'u') {
        int lo = hex4(c + 3);
        if (lo >= 0xDC00 && lo < 0xE000) {
          cp = 0x10000 + (((u32)u - 0xD800) << 10) + ((u32)lo - 0xDC00);
          c += 6;
        }
      }
      d += put_utf8(d, cp);
      break;
    }
    default: *d++ = *c;
    }
  }
  *d = 0;
  p->s = e + 1;
  return out;
}

static Json *parse_value(Parser *p, int depth);

static void append(Json *parent, Json *child) {
  if (parent->tail) parent->tail->next = child;
  else parent->head = child;
  parent->tail = child;
  parent->count++;
}

static Json *parse_value(Parser *p, int depth) {
  if (depth > 64) {
    fail(p, "nested too deeply");
    return NULL;
  }
  skip_ws(p);
  Json *j = arena_alloc(p->ar, sizeof(Json));
  char c = *p->s;
  if (c == '{' || c == '[') {
    bool obj = c == '{';
    char close = obj ? '}' : ']';
    j->type = obj ? J_OBJ : J_ARR;
    p->s++;
    skip_ws(p);
    if (*p->s == close) {
      p->s++;
      return j;
    }
    for (;;) {
      const char *key = NULL;
      if (obj) {
        skip_ws(p);
        if (*p->s != '"') {
          fail(p, "expected a member name");
          return NULL;
        }
        key = parse_string(p);
        if (!key) return NULL;
        skip_ws(p);
        if (*p->s != ':') {
          fail(p, "expected ':'");
          return NULL;
        }
        p->s++;
      }
      Json *v = parse_value(p, depth + 1);
      if (!v) return NULL;
      v->key = key;
      append(j, v);
      skip_ws(p);
      if (*p->s == ',') {
        p->s++;
        continue;
      }
      if (*p->s == close) {
        p->s++;
        return j;
      }
      fail(p, obj ? "expected ',' or '}'" : "expected ',' or ']'");
      return NULL;
    }
  }
  if (c == '"') {
    j->type = J_STR;
    j->str = parse_string(p);
    return j->str ? j : NULL;
  }
  if (!strncmp(p->s, "true", 4)) return p->s += 4, j->type = J_BOOL, j->b = true, j;
  if (!strncmp(p->s, "false", 5)) return p->s += 5, j->type = J_BOOL, j;
  if (!strncmp(p->s, "null", 4)) return p->s += 4, j;
  if (c == '-' || (c >= '0' && c <= '9')) {
    char *end;
    j->type = J_NUM;
    j->num = strtod(p->s, &end);
    p->s = end;
    return j;
  }
  fail(p, "unexpected character");
  return NULL;
}

Json *json_parse(Arena *ar, const char *text, char *err, size_t errlen) {
  Parser p = {text, text, ar, err, errlen, false};
  Json *j = parse_value(&p, 0);
  if (!j) return NULL;
  skip_ws(&p);
  if (*p.s) {
    fail(&p, "trailing characters");
    return NULL;
  }
  return j;
}

Json *json_get(const Json *obj, const char *key) {
  if (!obj || obj->type != J_OBJ) return NULL;
  for (Json *m = obj->head; m; m = m->next)
    if (!strcmp(m->key, key)) return m;
  return NULL;
}
const char *json_str(const Json *obj, const char *key, const char *fallback) {
  Json *m = json_get(obj, key);
  return m && m->type == J_STR ? m->str : fallback;
}
double json_num(const Json *obj, const char *key, double fallback) {
  Json *m = json_get(obj, key);
  return m && m->type == J_NUM ? m->num : fallback;
}
bool json_bool(const Json *obj, const char *key, bool fallback) {
  Json *m = json_get(obj, key);
  return m && m->type == J_BOOL ? m->b : fallback;
}

bool json_equal(const Json *a, const Json *b) {
  if (!a || !b) return a == b;
  if (a->type != b->type) return false;
  switch (a->type) {
  case J_NULL: return true;
  case J_BOOL: return a->b == b->b;
  case J_NUM: return a->num == b->num;
  case J_STR: return !strcmp(a->str, b->str);
  case J_ARR: {
    if (a->count != b->count) return false;
    Json *x = a->head, *y = b->head;
    for (; x; x = x->next, y = y->next)
      if (!json_equal(x, y)) return false;
    return true;
  }
  case J_OBJ:
    if (a->count != b->count) return false;
    for (Json *x = a->head; x; x = x->next)
      if (!json_equal(x, json_get(b, x->key))) return false;
    return true;
  }
  return false;
}

Json *json_new(Arena *ar, JsonType t) {
  Json *j = arena_alloc(ar, sizeof(Json));
  j->type = t;
  return j;
}
Json *json_add(Arena *ar, Json *parent, const char *key, Json *child) {
  child->key = key ? arena_str(ar, key) : NULL;
  append(parent, child);
  return child;
}
Json *json_add_num(Arena *ar, Json *parent, const char *key, double v) {
  Json *j = json_new(ar, J_NUM);
  j->num = v;
  return json_add(ar, parent, key, j);
}
Json *json_add_str(Arena *ar, Json *parent, const char *key, const char *s) {
  Json *j = json_new(ar, J_STR);
  j->str = arena_str(ar, s);
  return json_add(ar, parent, key, j);
}
Json *json_add_bool(Arena *ar, Json *parent, const char *key, bool v) {
  Json *j = json_new(ar, J_BOOL);
  j->b = v;
  return json_add(ar, parent, key, j);
}
Json *json_clone(Arena *ar, const Json *j) {
  Json *c = json_new(ar, j->type);
  c->b = j->b;
  c->num = j->num;
  c->str = j->str ? arena_str(ar, j->str) : NULL;
  for (Json *m = j->head; m; m = m->next) json_add(ar, c, m->key, json_clone(ar, m));
  return c;
}

// ------------------------------------------------------------------ writer
typedef struct {
  char *s;
} Out;
static void put(Out *o, const char *t) {
  size_t n = strlen(t);
  memcpy(arr_add(o->s, (int)n), t, n);
}
static void put_num(Out *o, double v) {
  char b[64];
  if (v == floor(v) && fabs(v) < 1e15) {
    snprintf(b, sizeof b, "%.0f", v);
  } else {
    for (int prec = 6; prec <= 17; prec++) {
      snprintf(b, sizeof b, "%.*g", prec, v);
      if (strtod(b, NULL) == v) break;
    }
  }
  put(o, b);
}
static void put_str(Out *o, const char *s) {
  arr_push(o->s, '"');
  for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
    char b[8];
    if (*c == '"') put(o, "\\\"");
    else if (*c == '\\') put(o, "\\\\");
    else if (*c == '\n') put(o, "\\n");
    else if (*c == '\t') put(o, "\\t");
    else if (*c == '\r') put(o, "\\r");
    else if (*c < 0x20) snprintf(b, sizeof b, "\\u%04x", *c), put(o, b);
    else arr_push(o->s, (char)*c);
  }
  arr_push(o->s, '"');
}
static bool all_numbers(const Json *a) {
  for (Json *m = a->head; m; m = m->next)
    if (m->type != J_NUM) return false;
  return true;
}
static bool short_scalar(const Json *j) {
  if (j->type == J_ARR) return all_numbers(j) && j->count <= 8;
  return j->type != J_OBJ;
}
// [[x, y], [x, y]] tables stay on one line: they are read as one thing.
static bool compact(const Json *a) {
  if (a->type != J_ARR) return false;
  if (all_numbers(a)) return a->count <= 16;
  if (a->count > 12) return false;
  for (Json *m = a->head; m; m = m->next)
    if (!(m->type == J_ARR && all_numbers(m) && m->count <= 4)) return false;
  return a->count > 0;
}
static void indent(Out *o, int n) {
  for (int i = 0; i < n; i++) put(o, "  ");
}
static void write_value(Out *o, const Json *j, int depth);
static void write_inline(Out *o, const Json *j) {
  if (j->type == J_ARR) {
    arr_push(o->s, '[');
    for (Json *m = j->head; m; m = m->next) {
      write_inline(o, m);
      if (m->next) put(o, ", ");
    }
    arr_push(o->s, ']');
  } else write_value(o, j, 0);
}
static void write_value(Out *o, const Json *j, int depth) {
  switch (j->type) {
  case J_NULL: put(o, "null"); break;
  case J_BOOL: put(o, j->b ? "true" : "false"); break;
  case J_NUM: put_num(o, j->num); break;
  case J_STR: put_str(o, j->str); break;
  case J_ARR:
    if (!j->count) {
      put(o, "[]");
    } else if (compact(j)) {
      write_inline(o, j);
    } else {
      put(o, "[\n");
      for (Json *m = j->head; m; m = m->next) {
        indent(o, depth + 1);
        write_value(o, m, depth + 1);
        put(o, m->next ? ",\n" : "\n");
      }
      indent(o, depth);
      arr_push(o->s, ']');
    }
    break;
  case J_OBJ: {
    if (!j->count) {
      put(o, "{}");
      break;
    }
    bool small = j->count <= 3;
    for (Json *m = j->head; m && small; m = m->next)
      if (!short_scalar(m) || (m->type == J_STR && strlen(m->str) > 30)) small = false;
    if (small) {
      put(o, "{ ");
      for (Json *m = j->head; m; m = m->next) {
        put_str(o, m->key);
        put(o, ": ");
        write_value(o, m, depth + 1);
        if (m->next) put(o, ", ");
      }
      put(o, " }");
      break;
    }
    put(o, "{\n");
    for (Json *m = j->head; m; m = m->next) {
      indent(o, depth + 1);
      put_str(o, m->key);
      put(o, ": ");
      write_value(o, m, depth + 1);
      put(o, m->next ? ",\n" : "\n");
    }
    indent(o, depth);
    arr_push(o->s, '}');
    break;
  }
  }
}

char *json_write(const Json *j) {
  Out o = {0};
  write_value(&o, j, 0);
  put(&o, "\n");
  int n = arr_len(o.s);
  char *r = malloc((size_t)n + 1);
  memcpy(r, o.s, (size_t)n);
  r[n] = 0;
  arr_free(o.s);
  return r;
}
