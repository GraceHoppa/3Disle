// `3disle export`: one device or tour as one HTML file that opens from disk, with the engine and only
// the data it reaches inside. Files keep the relative paths the data uses, so its links still resolve.
#include "app.h"
#include "json.h"
#include "web_data.h"

typedef struct {
  char **keys, **paths, **texts;
} Pack;

static bool pack_has(const Pack *p, const char *key) {
  for (int i = 0; i < arr_len(p->keys); i++)
    if (!strcmp(p->keys[i], key)) return true;
  return false;
}

static char *dup_s(const char *s) {
  size_t n = strlen(s) + 1;
  char *d = malloc(n);
  memcpy(d, s, n);
  return d;
}

static bool pack_add(Pack *p, const char *key, const char *disk, char *err, size_t errlen);

// Everything a file reaches: `detail` anywhere inside it, and a tour's level files.
static bool walk_links(Pack *p, const Json *j, const char *key, const char *disk, char *err, size_t errlen) {
  if (j->type == J_OBJ || j->type == J_ARR) {
    for (const Json *c = j->head; c; c = c->next) {
      bool link = c->type == J_STR && c->key && !strcmp(c->key, "detail");
      if (link && c->str[0]) {
        Arena ar = {0};
        bool ok = pack_add(p, path_join(&ar, key, c->str), path_join(&ar, disk, c->str), err, errlen);
        arena_free(&ar);
        if (!ok) return false;
      }
      if (!walk_links(p, c, key, disk, err, errlen)) return false;
    }
  }
  return true;
}

static bool pack_add(Pack *p, const char *key, const char *disk, char *err, size_t errlen) {
  if (pack_has(p, key)) return true;
  char *text = file_read(disk, NULL);
  if (!text) {
    snprintf(err, errlen, "%s links to a file that isn't there", disk);
    return false;
  }
  arr_push(p->keys, dup_s(key));
  arr_push(p->paths, dup_s(disk));
  arr_push(p->texts, text);
  Arena ar = {0};
  Json *j = json_parse(&ar, text, err, errlen);
  bool ok = j != NULL;
  if (j) {
    const Json *lv = json_get(j, "levels");
    if (lv && lv->type == J_ARR)
      for (const Json *l = lv->head; l && ok; l = l->next) {
        const char *f = json_str(l, "file", "");
        ok = pack_add(p, path_join(&ar, key, f), path_join(&ar, disk, f), err, errlen);
      }
    if (ok) ok = walk_links(p, j, key, disk, err, errlen);
  }
  arena_free(&ar);
  return ok;
}

typedef struct {
  char *d;
  size_t n, cap;
} Out;
static void out_add(Out *o, const char *s, size_t n) {
  if (o->n + n + 1 > o->cap) {
    o->cap = (o->n + n + 1) * 2;
    o->d = realloc(o->d, o->cap);
  }
  memcpy(o->d + o->n, s, n);
  o->n += n;
  o->d[o->n] = 0;
}
static void out_str(Out *o, const char *s) { out_add(o, s, strlen(s)); }

// JSON text inside <script>: no raw "<", so nothing in it can end the script.
static void out_json_string(Out *o, const char *s) {
  out_add(o, "\"", 1);
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    char esc[8];
    if (c == '"' || c == '\\') {
      esc[0] = '\\', esc[1] = (char)c;
      out_add(o, esc, 2);
    } else if (c < 0x20 || c == '<' || c == '>' || c == '&') {
      snprintf(esc, sizeof esc, "\\u%04x", c);
      out_add(o, esc, 6);
    } else {
      out_add(o, (const char *)&c, 1);
    }
  }
  out_add(o, "\"", 1);
}

static void out_base64(Out *o, const unsigned char *d, size_t n) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char q[4];
  for (size_t i = 0; i < n; i += 3) {
    unsigned v = (unsigned)d[i] << 16 | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    q[0] = A[v >> 18 & 63], q[1] = A[v >> 12 & 63];
    q[2] = i + 1 < n ? A[v >> 6 & 63] : '=';
    q[3] = i + 2 < n ? A[v & 63] : '=';
    out_add(o, q, 4);
  }
}

static void out_escaped(Out *o, const char *s) {
  for (; *s; s++) {
    if (*s == '<') out_str(o, "&lt;");
    else if (*s == '&') out_str(o, "&amp;");
    else out_add(o, s, 1);
  }
}

bool export_page(const char *entry, const char *out_path, char *err, size_t errlen) {
  if (!ASSET_web_wasm_len) {
    snprintf(err, errlen, "this build has no web engine: it needs clang with wasm-ld at build time");
    return false;
  }
  Pack p = {0};
  const char *slash = strrchr(entry, '/');
  const char *base = slash ? slash + 1 : entry;
  if (!pack_add(&p, base, entry, err, errlen)) return false;

  Arena ar = {0};
  Json *j = json_parse(&ar, p.texts[0], NULL, 0);
  const char *title = json_str(j, "name", "3Disle");

  Out o = {0};
  const char *page = (const char *)ASSET_web_page;
  // Fills the template's @MARKS@ in order of appearance.
  for (const char *c = page; c < page + ASSET_web_page_len;) {
    const char *m = memchr(c, '@', (size_t)(page + ASSET_web_page_len - c));
    if (!m) {
      out_add(&o, c, (size_t)(page + ASSET_web_page_len - c));
      break;
    }
    out_add(&o, c, (size_t)(m - c));
    if (!strncmp(m, "@TITLE@", 7)) {
      out_escaped(&o, title);
      c = m + 7;
    } else if (!strncmp(m, "@ENTRY@", 7)) {
      out_str(&o, base);
      c = m + 7;
    } else if (!strncmp(m, "@WASM@", 6)) {
      out_base64(&o, ASSET_web_wasm, ASSET_web_wasm_len);
      c = m + 6;
    } else if (!strncmp(m, "@GLUE@", 6)) {
      out_add(&o, (const char *)ASSET_web_glue, ASSET_web_glue_len);
      c = m + 6;
    } else if (!strncmp(m, "@DATA@", 6)) {
      out_str(&o, "{");
      for (int i = 0; i < arr_len(p.keys); i++) {
        if (i) out_str(&o, ",\n");
        out_json_string(&o, p.keys[i]);
        out_str(&o, ":");
        out_json_string(&o, p.texts[i]);
      }
      out_str(&o, "}");
      c = m + 6;
    } else {
      out_add(&o, m, 1);
      c = m + 1;
    }
  }
  bool ok = file_write(out_path, o.d, o.n);
  if (!ok) snprintf(err, errlen, "can't write %s", out_path);
  else printf("%s: %d files, %.1f MB\n", out_path, arr_len(p.keys), (double)o.n / 1048576);
  free(o.d);
  arena_free(&ar);
  for (int i = 0; i < arr_len(p.keys); i++) free(p.keys[i]), free(p.paths[i]), free(p.texts[i]);
  arr_free(p.keys), arr_free(p.paths), arr_free(p.texts);
  return ok;
}
