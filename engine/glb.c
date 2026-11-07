#include "glb.h"
#include "json.h"
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define MAX_NODES 200000
#define MAX_VERTS 40000000

static u32 rd32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static uint16_t rd16(const u8 *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const u8 *p) {
  float v;
  memcpy(&v, p, 4);
  return v;
}

// ------------------------------------------------------------------ the JSON's numbers, safely
static int to_int(double v, int fallback) { return v >= -2147483000.0 && v <= 2147483000.0 ? (int)v : fallback; }
static int jint(const Json *o, const char *key, int fallback) { return to_int(json_num(o, key, fallback), fallback); }
static double dbl_at(const Json *arr, int i, double fallback) {
  if (!arr || arr->type != J_ARR) return fallback;
  const Json *e = arr->head;
  for (int k = 0; e && k < i; k++) e = e->next;
  return e && e->type == J_NUM ? e->num : fallback;
}
static const Json **index_array(Arena *ar, const Json *arr, int *n) {
  *n = arr && arr->type == J_ARR ? arr->count : 0;
  const Json **out = arena_alloc(ar, sizeof(*out) * (size_t)(*n + 1));
  int i = 0;
  if (*n)
    for (const Json *e = arr->head; e && i < *n; e = e->next) out[i++] = e;
  *n = i;
  return out;
}

typedef struct {
  const u8 *bin;
  size_t bin_len;
  const Json **views, **accessors;
  int nviews, naccessors;
} Ctx;

static bool view_range(const Ctx *c, int view, size_t *off, size_t *len, size_t *stride) {
  if (view < 0 || view >= c->nviews) return false;
  const Json *v = c->views[view];
  double o = json_num(v, "byteOffset", 0), l = json_num(v, "byteLength", -1);
  if (o < 0 || l < 0 || o + l > (double)c->bin_len) return false;
  *off = (size_t)o, *len = (size_t)l;
  double s = json_num(v, "byteStride", 0);
  *stride = s > 0 && s <= 2048 ? (size_t)s : 0;
  return true;
}

// Where an accessor's elements are: false unless every element lies inside the buffer.
typedef struct {
  const u8 *p;
  size_t stride;
  int count, comp, ncomp;
  bool normalized;
} Acc;

static int comp_size(int c) { return c == 5120 || c == 5121 ? 1 : c == 5122 || c == 5123 ? 2 : c == 5125 || c == 5126 ? 4 : 0; }
static int type_comps(const char *t) { return !t ? 0 : !strcmp(t, "SCALAR") ? 1 : !strcmp(t, "VEC2") ? 2 : !strcmp(t, "VEC3") ? 3 : !strcmp(t, "VEC4") ? 4 : 0; }

static bool accessor(const Ctx *c, int index, Acc *a) {
  if (index < 0 || index >= c->naccessors) return false;
  const Json *j = c->accessors[index];
  if (json_get(j, "sparse")) return false;
  size_t off, len, stride;
  if (!view_range(c, jint(j, "bufferView", -1), &off, &len, &stride)) return false;
  double extra = json_num(j, "byteOffset", 0), cnt = json_num(j, "count", -1);
  if (extra < 0 || cnt < 0 || cnt > MAX_VERTS) return false;
  a->comp = jint(j, "componentType", 0);
  a->ncomp = type_comps(json_str(j, "type", NULL));
  int cs = comp_size(a->comp);
  if (!cs || !a->ncomp) return false;
  size_t elem = (size_t)cs * (size_t)a->ncomp;
  a->stride = stride ? stride : elem;
  a->count = (int)cnt;
  a->normalized = json_bool(j, "normalized", false);
  if (stride && stride < elem) return false;
  if ((size_t)extra > len) return false;
  if (a->count) {
    uint64_t need = (uint64_t)a->stride * (uint64_t)(a->count - 1) + elem;
    if ((uint64_t)extra + need > len) return false;
  }
  a->p = c->bin + off + (size_t)extra;
  return true;
}

static float acc_float(const Acc *a, int i, int k) {
  const u8 *p = a->p + a->stride * (size_t)i + (size_t)comp_size(a->comp) * (size_t)k;
  switch (a->comp) {
  case 5126: return rdf(p);
  case 5121: return a->normalized ? (float)p[0] / 255 : (float)p[0];
  case 5123: return a->normalized ? (float)rd16(p) / 65535 : (float)rd16(p);
  case 5120: return a->normalized ? maxf((float)(signed char)p[0] / 127, -1) : (float)(signed char)p[0];
  case 5122: return a->normalized ? maxf((float)(short)rd16(p) / 32767, -1) : (float)(short)rd16(p);
  default: return 0;
  }
}

static u32 acc_index(const Acc *a, int i) {
  const u8 *p = a->p + a->stride * (size_t)i;
  switch (a->comp) {
  case 5121: return p[0];
  case 5123: return rd16(p);
  case 5125: return rd32(p);
  default: return 0;
  }
}

// ------------------------------------------------------------------ the node tree
static Mat4 node_matrix(const Json *n) {
  const Json *m = json_get(n, "matrix");
  if (m && m->type == J_ARR && m->count == 16) {
    Mat4 r;
    for (int i = 0; i < 16; i++) r.m[i] = (float)dbl_at(m, i, i % 5 == 0);
    return r;
  }
  const Json *t = json_get(n, "translation"), *q = json_get(n, "rotation"), *s = json_get(n, "scale");
  float x = (float)dbl_at(q, 0, 0), y = (float)dbl_at(q, 1, 0), z = (float)dbl_at(q, 2, 0), w = (float)dbl_at(q, 3, 1);
  float sx = (float)dbl_at(s, 0, 1), sy = (float)dbl_at(s, 1, 1), sz = (float)dbl_at(s, 2, 1);
  Mat4 r = {{0}};
  r.m[0] = (1 - 2 * (y * y + z * z)) * sx, r.m[1] = 2 * (x * y + z * w) * sx, r.m[2] = 2 * (x * z - y * w) * sx;
  r.m[4] = 2 * (x * y - z * w) * sy, r.m[5] = (1 - 2 * (x * x + z * z)) * sy, r.m[6] = 2 * (y * z + x * w) * sy;
  r.m[8] = 2 * (x * z + y * w) * sz, r.m[9] = 2 * (y * z - x * w) * sz, r.m[10] = (1 - 2 * (x * x + y * y)) * sz;
  r.m[12] = (float)dbl_at(t, 0, 0), r.m[13] = (float)dbl_at(t, 1, 0), r.m[14] = (float)dbl_at(t, 2, 0);
  r.m[15] = 1;
  return r;
}

static float mat3_det(Mat4 m) {
  return m.m[0] * (m.m[5] * m.m[10] - m.m[9] * m.m[6]) - m.m[4] * (m.m[1] * m.m[10] - m.m[9] * m.m[2]) + m.m[8] * (m.m[1] * m.m[6] - m.m[5] * m.m[2]);
}

static void flat_normals(ModelPrim *p) {
  arr_set_len(p->nor, arr_len(p->pos));
  memset(p->nor, 0, sizeof(Vec3) * (size_t)arr_len(p->pos));
  for (int i = 0; i + 2 < arr_len(p->idx); i += 3) {
    Vec3 a = p->pos[p->idx[i]], b = p->pos[p->idx[i + 1]], c = p->pos[p->idx[i + 2]];
    Vec3 n = v3cross(v3sub(b, a), v3sub(c, a));
    for (int k = 0; k < 3; k++) p->nor[p->idx[i + k]] = v3add(p->nor[p->idx[i + k]], n);
  }
  for (int i = 0; i < arr_len(p->nor); i++) {
    float l = v3len(p->nor[i]);
    p->nor[i] = l > 0 ? v3scale(p->nor[i], 1 / l) : v3(0, 1, 0);
  }
}

// One primitive under a node's matrix, or nothing if it isn't triangles that can be read.
static bool bake_prim(ModelFile *f, const Ctx *c, const Json *prim, Mat4 world, int *out) {
  const Json *attrs = json_get(prim, "attributes");
  if (!attrs) return false;
  int mode = jint(prim, "mode", 4);
  if (mode != 4 && mode != 5 && mode != 6) return false;
  if (json_get(prim, "extensions") && json_get(json_get(prim, "extensions"), "KHR_draco_mesh_compression")) return false;
  Acc pos;
  if (!accessor(c, jint(attrs, "POSITION", -1), &pos) || pos.ncomp != 3 || pos.comp != 5126 || pos.count < 3) return false;
  ModelPrim p = {0};
  p.material = jint(prim, "material", -1);
  p.box = box_empty();
  arr_set_len(p.pos, pos.count);
  for (int i = 0; i < pos.count; i++) {
    Vec3 w = m4point(world, v3(acc_float(&pos, i, 0), acc_float(&pos, i, 1), acc_float(&pos, i, 2)));
    if (!isfinite(w.x) || !isfinite(w.y) || !isfinite(w.z)) w = v3(0, 0, 0);
    p.pos[i] = w;
    box_add(&p.box, w);
  }
  Acc nor;
  bool has_nor = accessor(c, jint(attrs, "NORMAL", -1), &nor) && nor.ncomp == 3 && nor.count == pos.count;
  if (has_nor) {
    // Normals turn by the inverse transpose, so a stretched object still shades right.
    Mat4 inv = m4invert(world);
    arr_set_len(p.nor, pos.count);
    for (int i = 0; i < pos.count; i++) {
      float x = acc_float(&nor, i, 0), y = acc_float(&nor, i, 1), z = acc_float(&nor, i, 2);
      Vec3 n = v3(inv.m[0] * x + inv.m[1] * y + inv.m[2] * z, inv.m[4] * x + inv.m[5] * y + inv.m[6] * z, inv.m[8] * x + inv.m[9] * y + inv.m[10] * z);
      float l = v3len(n);
      p.nor[i] = l > 1e-12f && isfinite(l) ? v3scale(n, 1 / l) : v3(0, 1, 0);
    }
  }
  Acc uv;
  if (accessor(c, jint(attrs, "TEXCOORD_0", -1), &uv) && uv.ncomp == 2 && uv.count == pos.count) {
    arr_set_len(p.uv, pos.count);
    for (int i = 0; i < pos.count; i++) p.uv[i] = v2(acc_float(&uv, i, 0), acc_float(&uv, i, 1));
  }
  // Indices, checked, and always as triangles.
  Acc ind;
  int raw_n = pos.count;
  bool has_ind = json_get(prim, "indices") != NULL;
  if (has_ind) {
    if (!accessor(c, jint(prim, "indices", -1), &ind) || ind.ncomp != 1 || (ind.comp != 5121 && ind.comp != 5123 && ind.comp != 5125)) goto bad;
    raw_n = ind.count;
  }
#define RAW(i) (has_ind ? acc_index(&ind, (i)) : (u32)(i))
  {
    bool flip = mat3_det(world) < 0;
    int tris = mode == 4 ? raw_n / 3 : maxi(raw_n - 2, 0);
    if (!tris) goto bad;
    arr_reserve(p.idx, tris * 3);
    for (int t = 0; t < tris; t++) {
      u32 a, b, d;
      if (mode == 4) a = RAW(t * 3), b = RAW(t * 3 + 1), d = RAW(t * 3 + 2);
      else if (mode == 5) a = RAW(t), b = RAW(t + 1 + (t & 1)), d = RAW(t + 2 - (t & 1));
      else a = RAW(0), b = RAW(t + 1), d = RAW(t + 2);
      if (a >= (u32)pos.count || b >= (u32)pos.count || d >= (u32)pos.count) goto bad;
      arr_push(p.idx, a);
      arr_push(p.idx, flip ? d : b);
      arr_push(p.idx, flip ? b : d);
    }
  }
#undef RAW
  if (!has_nor) flat_normals(&p);
  *out = arr_len(f->prims);
  arr_push(f->prims, p);
  return true;
bad:
  arr_free(p.pos);
  arr_free(p.nor);
  arr_free(p.uv);
  arr_free(p.idx);
  return false;
}

typedef struct {
  ModelFile *f;
  const Ctx *c;
  const Json **nodes, **meshes;
  int nnodes, nmeshes;
  bool *seen;
} Walk;

// Parents before children. A node reached twice is a broken file, not a loop to follow.
static void walk_node(Walk *w, int index, int parent, Mat4 up, int depth) {
  if (index < 0 || index >= w->nnodes || w->seen[index] || depth > 512) return;
  w->seen[index] = true;
  const Json *n = w->nodes[index];
  Mat4 world = m4mul(up, node_matrix(n));
  ModelObject o = {0};
  char auto_name[32];
  snprintf(auto_name, sizeof auto_name, "Object %d", index);
  o.name = arena_str(&w->f->ar, json_str(n, "name", auto_name));
  o.parent = parent;
  int me = arr_len(w->f->objects);
  int mesh = jint(n, "mesh", -1);
  if (mesh >= 0 && mesh < w->nmeshes) {
    const Json *prims = json_get(w->meshes[mesh], "primitives");
    if (prims && prims->type == J_ARR)
      for (const Json *p = prims->head; p; p = p->next) {
        int at;
        if (bake_prim(w->f, w->c, p, world, &at)) arr_push(o.prims, at);
      }
  }
  arr_push(w->f->objects, o);
  const Json *kids = json_get(n, "children");
  if (kids && kids->type == J_ARR)
    for (const Json *k = kids->head; k; k = k->next)
      if (k->type == J_NUM) walk_node(w, to_int(k->num, -1), me, world, depth + 1);
}

// ------------------------------------------------------------------ materials
static int texture_image(const Json *textures_arr[], int ntex, const Json *info) {
  if (!info) return -1;
  int t = jint(info, "index", -1);
  if (t < 0 || t >= ntex) return -1;
  return jint(textures_arr[t], "source", -1);
}

static void read_materials(ModelFile *f, const Ctx *c, const Json *root) {
  int ntex = 0, nmat = 0, nimg = 0;
  const Json **tex = index_array(&f->ar, json_get(root, "textures"), &ntex);
  const Json **mats = index_array(&f->ar, json_get(root, "materials"), &nmat);
  const Json **imgs = index_array(&f->ar, json_get(root, "images"), &nimg);
  for (int i = 0; i < nimg; i++) {
    ModelImage im = {0};
    size_t off, len, st;
    if (view_range(c, jint(imgs[i], "bufferView", -1), &off, &len, &st) && len < 0x7fffffff) {
      im.data = c->bin + off;
      im.len = (int)len;
    }
    arr_push(f->images, im);
  }
  for (int i = 0; i < nmat; i++) {
    const Json *m = mats[i], *pbr = json_get(m, "pbrMetallicRoughness");
    const Json *col = json_get(pbr, "baseColorFactor"), *em = json_get(m, "emissiveFactor");
    ModelMaterial o = {0};
    o.name = arena_str(&f->ar, json_str(m, "name", ""));
    o.color = v3((float)dbl_at(col, 0, 1), (float)dbl_at(col, 1, 1), (float)dbl_at(col, 2, 1));
    o.alpha = (float)dbl_at(col, 3, 1);
    o.metalness = clampf((float)json_num(pbr, "metallicFactor", 1), 0, 1);
    o.roughness = clampf((float)json_num(pbr, "roughnessFactor", 1), 0, 1);
    o.emissive = v3((float)dbl_at(em, 0, 0), (float)dbl_at(em, 1, 0), (float)dbl_at(em, 2, 0));
    o.image = texture_image(tex, ntex, json_get(pbr, "baseColorTexture"));
    o.mr_image = texture_image(tex, ntex, json_get(pbr, "metallicRoughnessTexture"));
    if (o.image >= arr_len(f->images)) o.image = -1;
    if (o.mr_image >= arr_len(f->images)) o.mr_image = -1;
    o.blend = !strcmp(json_str(m, "alphaMode", "OPAQUE"), "BLEND");
    o.two_sided = json_bool(m, "doubleSided", false);
    arr_push(f->materials, o);
  }
}

static const char *credit_of(ModelFile *f, const Json *root) {
  const Json *ex = json_get(json_get(root, "asset"), "extras");
  if (!ex) return NULL;
  const char *t = json_str(ex, "title", NULL), *a = json_str(ex, "author", NULL), *l = json_str(ex, "license", NULL);
  if (!a && !l) return NULL;
  char b[1024];
  snprintf(b, sizeof b, "%s%s%s%s%s", t ? t : "", t && a ? " \xC2\xB7 " : "", a ? a : "", l ? " \xC2\xB7 " : "", l ? l : "");
  return arena_str(&f->ar, b);
}

static void finish(ModelFile *f) {
  f->box = box_empty();
  for (int i = 0; i < arr_len(f->prims); i++) box_union(&f->box, f->prims[i].box);
  // Who has geometry, here or below: children come after their parents.
  for (int i = arr_len(f->objects) - 1; i >= 0; i--) {
    ModelObject *o = &f->objects[i];
    if (arr_len(o->prims)) o->has_geometry = true;
    if (o->has_geometry && o->parent >= 0) f->objects[o->parent].has_geometry = true;
  }
}

static bool parse_glb(ModelFile *f, char *err, size_t errlen) {
  const u8 *b = f->bytes;
  size_t n = f->nbytes;
#define FAIL(...) do { if (err) snprintf(err, errlen, __VA_ARGS__); return false; } while (0)
  if (n < 20) FAIL("too short to be a GLB");
  if (rd32(b + 4) != 2) FAIL("GLB version %u: only glTF 2 is supported", rd32(b + 4));
  size_t at = 12;
  const u8 *json_at = NULL, *bin_at = NULL;
  size_t json_len = 0, bin_len = 0;
  while (at + 8 <= n) {
    size_t len = rd32(b + at);
    u32 type = rd32(b + at + 4);
    if (len > n - at - 8) FAIL("a GLB chunk runs past the end of the file");
    if (type == 0x4e4f534a && !json_at) json_at = b + at + 8, json_len = len;
    if (type == 0x004e4942 && !bin_at) bin_at = b + at + 8, bin_len = len;
    at += 8 + ((len + 3) & ~(size_t)3);
  }
  if (!json_at) FAIL("no JSON chunk in this GLB");
  char *text = malloc(json_len + 1);
  memcpy(text, json_at, json_len);
  text[json_len] = 0;
  char jerr[256] = "";
  Json *root = json_parse(&f->ar, text, jerr, sizeof jerr);
  free(text);
  if (!root) FAIL("the GLB's JSON is broken: %s", jerr);
  const Json *req = json_get(root, "extensionsRequired");
  if (req && req->type == J_ARR && req->head) FAIL("this GLB requires %s, which isn't supported", req->head->type == J_STR ? req->head->str : "an extension");
  Ctx c = {.bin = bin_at, .bin_len = bin_len};
  c.views = index_array(&f->ar, json_get(root, "bufferViews"), &c.nviews);
  c.accessors = index_array(&f->ar, json_get(root, "accessors"), &c.naccessors);
  Walk w = {.f = f, .c = &c};
  w.nodes = index_array(&f->ar, json_get(root, "nodes"), &w.nnodes);
  w.meshes = index_array(&f->ar, json_get(root, "meshes"), &w.nmeshes);
  if (w.nnodes > MAX_NODES) FAIL("too many objects");
  read_materials(f, &c, root);
  w.seen = calloc((size_t)w.nnodes + 1, 1);
  // The scene's roots, or everything no one claims as a child.
  int scn = jint(root, "scene", 0);
  int nscenes = 0;
  const Json **scenes = index_array(&f->ar, json_get(root, "scenes"), &nscenes);
  const Json *roots = scn >= 0 && scn < nscenes ? json_get(scenes[scn], "nodes") : NULL;
  if (roots && roots->type == J_ARR) {
    for (const Json *r = roots->head; r; r = r->next)
      if (r->type == J_NUM) walk_node(&w, to_int(r->num, -1), -1, m4identity(), 0);
  } else {
    for (int i = 0; i < w.nnodes; i++) walk_node(&w, i, -1, m4identity(), 0);
  }
  free(w.seen);
  if (!arr_len(f->prims)) FAIL("nothing in this GLB can be drawn");
  f->units = 1000;
  f->credit = credit_of(f, root);
  finish(f);
  return true;
#undef FAIL
}

// ------------------------------------------------------------------ STL
static void stl_push(ModelPrim *p, Vec3 a, Vec3 b, Vec3 c) {
  Vec3 n = v3cross(v3sub(b, a), v3sub(c, a));
  float l = v3len(n);
  n = l > 0 ? v3scale(n, 1 / l) : v3(0, 1, 0);
  Vec3 tri[3] = {a, b, c};
  for (int i = 0; i < 3; i++) {
    u32 at = (u32)arr_len(p->pos);
    arr_push(p->pos, tri[i]);
    arr_push(p->nor, n);
    arr_push(p->idx, at);
    box_add(&p->box, tri[i]);
  }
}

static bool parse_stl(ModelFile *f, char *err, size_t errlen) {
  const u8 *b = f->bytes;
  size_t n = f->nbytes;
  ModelPrim p = {0};
  p.material = -1;
  p.box = box_empty();
  bool binary = n >= 84 && (size_t)rd32(b + 80) <= (n - 84) / 50 && 84 + (size_t)rd32(b + 80) * 50 == n;
  if (binary) {
    u32 tris = rd32(b + 80);
    for (u32 t = 0; t < tris; t++) {
      const u8 *q = b + 84 + (size_t)t * 50 + 12;
      Vec3 v[3];
      for (int i = 0; i < 3; i++) v[i] = v3(rdf(q + i * 12), rdf(q + i * 12 + 4), rdf(q + i * 12 + 8));
      stl_push(&p, v[0], v[1], v[2]);
    }
  } else {
    // Text: `vertex x y z`, three to a facet. The buffer ends in a NUL (file_read adds one).
    const char *s = (const char *)b;
    Vec3 v[3];
    int have = 0;
    while ((s = strstr(s, "vertex"))) {
      s += 6;
      char *e;
      float x = (float)strtod(s, &e);
      s = e;
      float y = (float)strtod(s, &e);
      s = e;
      float z = (float)strtod(s, &e);
      s = e;
      v[have++] = v3(x, y, z);
      if (have == 3) {
        stl_push(&p, v[0], v[1], v[2]);
        have = 0;
      }
    }
  }
  if (!arr_len(p.idx)) {
    if (err) snprintf(err, errlen, "no triangles in this STL");
    return false;
  }
  f->is_stl = true;
  f->units = 1;
  arr_push(f->prims, p);
  ModelObject o = {0};
  const char *slash = strrchr(f->path, '/');
  o.name = arena_str(&f->ar, slash ? slash + 1 : f->path);
  o.parent = -1;
  arr_push(o.prims, 0);
  arr_push(f->objects, o);
  finish(f);
  return true;
}

// ------------------------------------------------------------------ the public side
ModelFile *modelfile_parse(u8 *bytes, size_t n, const char *path, char *err, size_t errlen) {
  ModelFile *f = calloc(1, sizeof *f);
  // Text STLs are searched as a string: it has to end.
  bytes = realloc(bytes, n + 1);
  bytes[n] = 0;
  f->bytes = bytes;
  f->nbytes = n;
  f->path = arena_str(&f->ar, path ? path : "");
  bool ok = n >= 4 && !memcmp(bytes, "glTF", 4) ? parse_glb(f, err, errlen) : parse_stl(f, err, errlen);
  if (!ok) {
    modelfile_free(f);
    return NULL;
  }
  return f;
}

void modelfile_free(ModelFile *f) {
  if (!f) return;
  for (int i = 0; i < arr_len(f->prims); i++) {
    arr_free(f->prims[i].pos);
    arr_free(f->prims[i].nor);
    arr_free(f->prims[i].uv);
    arr_free(f->prims[i].idx);
  }
  for (int i = 0; i < arr_len(f->objects); i++) arr_free(f->objects[i].prims);
  arr_free(f->prims);
  arr_free(f->objects);
  arr_free(f->materials);
  arr_free(f->images);
  free(f->bytes);
  arena_free(&f->ar);
  free(f);
}

static ModelFile **g_cache;

ModelFile *modelfile_load(const char *path, char *err, size_t errlen) {
  for (int i = 0; i < arr_len(g_cache); i++)
    if (!strcmp(g_cache[i]->path, path)) return g_cache[i];
  size_t n = 0;
  char *bytes = file_read(path, &n);
  if (!bytes) {
    if (err) snprintf(err, errlen, "can't read %s", path);
    return NULL;
  }
  ModelFile *f = modelfile_parse((u8 *)bytes, n, path, err, errlen);
  if (f) arr_push(g_cache, f);
  return f;
}

void modelfile_cache_clear(void) {
  for (int i = 0; i < arr_len(g_cache); i++) modelfile_free(g_cache[i]);
  arr_free(g_cache);
}

// three.js's rule for a node's name, which older data was written against.
static bool name_matches(const char *written, const char *asked) {
  if (!strcmp(written, asked)) return true;
  const char *a = written, *b = asked;
  for (;;) {
    while (*a && strchr("[].:/", *a)) a++;
    char ca = *a == ' ' ? '_' : *a;
    if (ca != *b) return false;
    if (!ca) return true;
    a++, b++;
  }
}

bool *modelfile_pick(const ModelFile *f, const char *const *names, int nnames) {
  bool *on = NULL;
  int n = arr_len(f->objects);
  arr_set_len(on, n);
  for (int i = 0; i < n; i++) {
    bool hit = nnames == 0;
    for (int k = 0; k < nnames && !hit; k++) hit = name_matches(f->objects[i].name, names[k]);
    // Parents come first, so a picked parent has already been decided.
    if (!hit && f->objects[i].parent >= 0) hit = on[f->objects[i].parent];
    on[i] = hit;
  }
  return on;
}

u8 *modelfile_image(const ModelFile *f, int image, int *w, int *h) {
  if (image < 0 || image >= arr_len(f->images) || !f->images[image].data) return NULL;
  int comp;
  return stbi_load_from_memory(f->images[image].data, f->images[image].len, w, h, &comp, 4);
}

int *modelfile_natural_parts(const ModelFile *f) {
  int *out = NULL;
  int n = arr_len(f->objects);
  // Start from the top; a lone way down is no choice, so keep going.
  int parent = -1;
  for (;;) {
    int only = -1, count = 0;
    bool own = false;
    for (int i = 0; i < n; i++)
      if (f->objects[i].parent == parent && f->objects[i].has_geometry) only = i, count++;
    if (parent >= 0 && arr_len(f->objects[parent].prims)) own = true;
    if (count == 1 && !own) {
      parent = only;
      continue;
    }
    if (!count || own) {
      if (parent >= 0) arr_push(out, parent);
      break;
    }
    for (int i = 0; i < n; i++)
      if (f->objects[i].parent == parent && f->objects[i].has_geometry) arr_push(out, i);
    break;
  }
  return out;
}
