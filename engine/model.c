#include "model.h"
#include "paint.h"

const float SPIN_SLOWDOWN = 64;

const MaterialPreset MATERIAL_PRESETS[] = {
    {"default", "#9aa3ad", 0.2f, 0.6f, 1, 0, false},
    {"pcb", "#0d3a1c", 0.1f, 0.55f, 1, 0, false},
    {"copper", "#d07a45", 1, 0.28f, 1, 0, false},
    {"gold", "#ffc35a", 1, 0.22f, 1, 0, false},
    {"aluminium", "#c3c8cf", 1, 0.32f, 1, 0, false},
    {"steel", "#8d939b", 1, 0.45f, 1, 0, false},
    {"gunmetal", "#24282d", 0.85f, 0.34f, 1, 0, false},
    {"chip", "#0c0d0e", 0.3f, 0.42f, 1, 0, false},
    {"die", "#8c96a6", 1, 0.08f, 1, 0, false},
    {"plastic", "#0b0d0f", 0.1f, 0.55f, 1, 0, false},
    {"rubber", "#16181b", 0, 0.92f, 1, 0, false},
    {"ceramic", "#b08a5a", 0.2f, 0.5f, 1, 0, false},
    {"fibreglass", "#c9b77a", 0, 0.75f, 1, 0, false},
    {"resin", "#2a2d33", 0, 0.55f, 0.4f, 0, true},
    {"glass", "#a9c4dc", 0, 0.12f, 0.35f, 0.6f, true},
};
const int MATERIAL_PRESET_COUNT = COUNT(MATERIAL_PRESETS);

const WireKind WIRE_KINDS[7] = {
    {"power", "#a3201a", "#ffae42", "Power", 0},   {"ground", "#141619", "#4f9dff", "Ground return", 0},
    {"phase", "#1b1d20", "#ff7a2f", "Motor phase", 1}, {"data", "#2a2e33", "#9cff3a", "Data", 2},
    {"signal", "#2a2e33", "#5fe0ff", "Analog signal", 3}, {"video", "#c9a91c", "#ffe066", "Video", 3},
    {"rf", "#1e2024", "#c48cff", "RF", 3},
};
const WireKind *wire_kind(const char *name) {
  for (int i = 0; name && i < COUNT(WIRE_KINDS); i++)
    if (!strcmp(WIRE_KINDS[i].name, name)) return &WIRE_KINDS[i];
  return &WIRE_KINDS[3];
}

Mat4 m4invert(Mat4 a) {
  const float *m = a.m;
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  Mat4 r;
  if (det == 0) return m4identity();
  det = 1 / det;
  for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
  return r;
}

// ------------------------------------------------------------------ texture cache
static Texture **g_textures;
static u32 g_tex_tick;

Texture *texture_cached(const char *key) {
  for (int i = 0; i < arr_len(g_textures); i++)
    if (!strcmp(g_textures[i]->key, key)) {
      g_textures[i]->last_used = ++g_tex_tick;
      return g_textures[i];
    }
  return NULL;
}
Texture *texture_put(const char *key, int w, int h, u8 *rgba, bool repeat) {
  Texture *t = calloc(1, sizeof *t);
  size_t n = strlen(key);
  t->key = malloc(n + 1);
  memcpy(t->key, key, n + 1);
  t->w = w, t->h = h, t->rgba = rgba, t->repeat = repeat;
  t->last_used = ++g_tex_tick;
  arr_push(g_textures, t);
  return t;
}
void texture_cache_each(void (*fn)(Texture *, void *), void *ud) {
  for (int i = 0; i < arr_len(g_textures); i++) fn(g_textures[i], ud);
}

// ------------------------------------------------------------------ materials
static const MaterialPreset *preset_of(const char *name) {
  for (int i = 0; name && i < MATERIAL_PRESET_COUNT; i++)
    if (!strcmp(MATERIAL_PRESETS[i].name, name)) return &MATERIAL_PRESETS[i];
  return &MATERIAL_PRESETS[0];
}

static Material material_of(const char *mat, const char *color, const char *glow) {
  const MaterialPreset *p = preset_of(mat);
  Material m = {0};
  m.mode = MAT_STANDARD;
  m.color = hex_linear(color ? color : p->color);
  m.metalness = p->metalness;
  m.roughness = p->roughness;
  m.opacity = p->opacity;
  m.transparent = p->transparent;
  m.clearcoat = p->clearcoat;
  m.uv_scale = 1;
  if (glow) {
    m.emissive = hex_linear(*glow ? glow : "#76b900");
    m.emissive_intensity = 2.2f;
  }
  return m;
}

// ------------------------------------------------------------------ the tree
static int add_node(Model *m, int parent, const char *name) {
  Node n = {0};
  n.parent = parent;
  n.first = n.last = n.next = -1;
  n.name = name ? name : "";
  n.info = "";
  n.wire = -1;
  n.layer_index = n.part_index = -1;
  n.self = n.box = box_empty();
  n.opacity = 1;
  n.level_now = 1;
  int id = arr_len(m->nodes);
  arr_push(m->nodes, n);
  if (parent >= 0) {
    Node *p = &m->nodes[parent];
    if (p->last >= 0) m->nodes[p->last].next = id;
    else p->first = id;
    p->last = id;
  }
  return id;
}

static Mat4 node_local(const Node *n) {
  Vec3 t = v3add(v3add(v3add(n->home, n->off_spread), n->off_room), n->off_anim);
  Mat4 l = m4translate(t);
  float a = n->rot0 + n->spin_angle;
  if (a != 0) l = m4mul(l, m4rot_y(a));
  return l;
}

static void update_worlds(Model *m) {
  for (int i = 0; i < arr_len(m->nodes); i++) {
    Node *n = &m->nodes[i];
    Mat4 l = node_local(n);
    n->world = n->parent >= 0 ? m4mul(m->nodes[n->parent].world, l) : l;
  }
}

// This node's own geometry in model space.
static Box self_box(const Node *n) {
  Box b = box_empty();
  if (!n->mesh || (n->flags & NF_DECAL)) return b;
  if (n->inst) {
    // A row: every copy's box, then that as a box in the world. Cheap, and a little generous on
    // turned copies, which is what layouts were tuned against.
    Box local = box_empty(), g = n->mesh->box;
    for (int c = 0; c < arr_len(n->inst); c++)
      for (int k = 0; k < 8; k++)
        box_add(&local, m4point(n->inst[c], v3(k & 1 ? g.max.x : g.min.x, k & 2 ? g.max.y : g.min.y, k & 4 ? g.max.z : g.min.z)));
    if (box_is_empty(local)) return b;
    for (int k = 0; k < 8; k++)
      box_add(&b, m4point(n->world, v3(k & 1 ? local.max.x : local.min.x, k & 2 ? local.max.y : local.min.y, k & 4 ? local.max.z : local.min.z)));
    return b;
  }
  for (int i = 0; i < arr_len(n->mesh->v); i++)
    box_add(&b, m4point(n->world, v3(n->mesh->v[i].px, n->mesh->v[i].py, n->mesh->v[i].pz)));
  return b;
}

// ------------------------------------------------------------------ copies
int part_copies(const PartSpec *p) {
  const Repeat *r = &p->repeat;
  double n = r->has_grid ? r->grid.x * r->grid.y : (is_set(r->count) && r->on ? r->count : 1);
  int k = (int)floor(n);
  return k < 1 ? 1 : k;
}

static u32 seed_of(const char *s) {
  u32 h = 7;
  // The string's UTF-16 units, as the layouts were first seeded.
  const unsigned char *c = (const unsigned char *)s;
  while (*c) {
    u32 cp;
    if (*c < 0x80) cp = *c++;
    else if (*c < 0xE0) cp = (u32)(c[0] & 31) << 6 | (c[1] & 63), c += 2;
    else if (*c < 0xF0) cp = (u32)(c[0] & 15) << 12 | (u32)(c[1] & 63) << 6 | (c[2] & 63), c += 3;
    else cp = (u32)(c[0] & 7) << 18 | (u32)(c[1] & 63) << 12 | (u32)(c[2] & 63) << 6 | (c[3] & 63), c += 4;
    h = h * 31 + cp;
  }
  return h;
}

Mat4 copy_matrix(const PartSpec *p, int i, int count, float x, float y, float z, const Footprint *avoid, int navoid) {
  const Repeat *r = &p->repeat;
  if (p->has_scatter) {
    Rng rng = rng_new(seed_of(p->name) + (u32)i * 7919u);
    float w = (float)p->scatter.x, d = (float)p->scatter.y, px = 0, pz = 0;
    for (int k = 0; k < 12; k++) {
      px = x + (float)(rng_next(&rng) - 0.5) * w;
      pz = z + (float)(rng_next(&rng) - 0.5) * d;
      bool hit = false;
      for (int f = 0; f < navoid && !hit; f++)
        hit = fabsf(px - avoid[f].x) < avoid[f].w / 2 + 1 && fabsf(pz - avoid[f].z) < avoid[f].d / 2 + 1;
      if (!hit) break;
    }
    Mat4 t = m4rot_y(rng_next(&rng) < 0.5 ? 0 : PI / 2);
    t.m[12] = px, t.m[13] = y, t.m[14] = pz;
    return t;
  }
  if (r->on && r->has_grid) {
    float sx = r->has_step ? (float)r->step.x : 0, sz = r->has_step ? (float)r->step.y : 0;
    int cols = maxi(1, (int)r->grid.x);
    return m4translate(v3(x + sx * (float)(i % cols), y, z + sz * (float)(i / cols)));
  }
  if (r->on && r->has_around) {
    float a = (r->has_start ? (float)r->start_angle : 0) * PI / 180 + (float)i / (float)count * TAU;
    Mat4 t = m4rot_y(-a);
    t.m[12] = x + cosf(a) * (float)r->around, t.m[13] = y, t.m[14] = z + sinf(a) * (float)r->around;
    return t;
  }
  float sx = r->on && r->has_step ? (float)r->step.x : 0, sz = r->on && r->has_step ? (float)r->step.y : 0;
  return m4translate(v3(x + sx * (float)i, y, z + sz * (float)i));
}

Footprint *layer_footprints(const LayerSpec *l) {
  Footprint *out = NULL;
  for (int k = 0; k < arr_len(l->parts); k++) {
    const PartSpec *p = &l->parts[k];
    if (p->shape == SHAPE_FILE || p->has_scatter) continue;
    float r = is_set(p->radius) ? (float)p->radius : 0.5f;
    float pw = p->shape == SHAPE_BOX ? (p->has_size ? (float)p->size.x : 1) : r * 2;
    float pd = p->shape == SHAPE_BOX ? (p->has_size ? (float)p->size.z : 1) : r * 2;
    float x = p->has_at ? (float)p->at.x : 0, z = p->has_at ? (float)p->at.y : 0;
    int n = part_copies(p);
    float x0 = INFINITY, x1 = -INFINITY, z0 = INFINITY, z1 = -INFINITY;
    for (int i = 0; i < n; i++) {
      Mat4 t = copy_matrix(p, i, n, x, 0, z, NULL, 0);
      float ax = t.m[12], az = t.m[14];
      const char *name = i < arr_len(p->repeat.names) ? p->repeat.names[i] : (n > 1 ? "" : p->name);
      arr_push(out, ((Footprint){name, ax, az, pw, pd, true}));
      x0 = minf(x0, ax - pw / 2), x1 = maxf(x1, ax + pw / 2);
      z0 = minf(z0, az - pd / 2), z1 = maxf(z1, az + pd / 2);
    }
    if (n > 1) arr_push(out, ((Footprint){p->name, (x0 + x1) / 2, (z0 + z1) / 2, x1 - x0, z1 - z0, false}));
  }
  return out;
}

// ------------------------------------------------------------------ painted surfaces
static u32 hash_str(u32 h, const char *s) {
  while (s && *s) h = (h ^ (u8)*s++) * 16777619u;
  return h;
}

static Texture *slab_texture(const LayerSpec *l, float w, float d, bool with_parts, u32 seed) {
  float pxmm = minf(64, maxf(4, 1536 / maxf(w, d)));
  Footprint *prints = with_parts ? layer_footprints(l) : NULL;
  u32 h = 2166136261u;
  for (int i = 0; i < arr_len(prints); i++) {
    char b[80];
    snprintf(b, sizeof b, "%.3f %.3f %.3f %.3f %d", (double)prints[i].x, (double)prints[i].z, (double)prints[i].w, (double)prints[i].d, prints[i].silk);
    h = hash_str(h, prints[i].name);
    h = hash_str(h, b);
  }
  char key[200];
  snprintf(key, sizeof key, "slab:%s:%.3f:%.3f:%u:%s:%u", l->surface, (double)w, (double)d, seed, l->color ? l->color : "", h);
  Texture *t = texture_cached(key);
  if (t) {
    arr_free(prints);
    return t;
  }
  int pw, ph;
  surface_size(w, d, pxmm, &pw, &ph);
  u8 *rgba = paint_surface(l->surface, pw, ph, pxmm, w, d, prints, arr_len(prints), seed, l->color);
  arr_free(prints);
  return texture_put(key, pw, ph, rgba, false);
}

#define FINISH_TILE 24.0f
static Texture *finish_texture(const char *kind, const char *base) {
  char key[120];
  snprintf(key, sizeof key, "finish:%s:%s", kind, base ? base : "");
  Texture *t = texture_cached(key);
  if (t) return t;
  u8 *rgba = paint_surface(kind, 256, 256, 256 / FINISH_TILE, FINISH_TILE, FINISH_TILE, NULL, 0, 1, base);
  return texture_put(key, 256, 256, rgba, true);
}

static void mark_chip(Model *m, int node, const PartSpec *p, float h) {
  if (p->shape != SHAPE_BOX) return;
  float w = p->has_size ? (float)p->size.x : 1, d = p->has_size ? (float)p->size.z : 1;
  char key[300];
  u8 *rgba = NULL;
  int tw = 0, th = 0;
  float inset = 1;
  Texture *tex = NULL;
  if (p->pattern) {
    float px = minf(96, 1024 / maxf(w, d));
    tw = maxi(16, (int)lroundf(w * px));
    th = maxi(16, (int)lroundf(d * px));
    int seed = 0;
    for (const unsigned char *c = (const unsigned char *)p->name; *c; c++) seed += (*c & 0xC0) != 0x80;
    int cx = p->has_cells ? (int)p->cells.x : 2, cy = p->has_cells ? (int)p->cells.y : 6;
    snprintf(key, sizeof key, "pat:%s:%d:%d:%s:%d:%d:%d", p->pattern, tw, th, p->color ? p->color : "", seed, cx, cy);
    tex = texture_cached(key);
    if (!tex) {
      rgba = paint_pattern(p->pattern, tw, th, p->color, seed, cx, cy);
      tex = texture_put(key, tw, th, rgba, false);
    }
  } else {
    if (!p->material || strcmp(p->material, "chip") || minf(w, d) < 2) return;
    tw = mini(1024, (int)lroundf(w * 48));
    th = mini(1024, (int)lroundf(d * 48));
    const char *text = p->marking ? p->marking : p->name;
    snprintf(key, sizeof key, "mark:%s:%d:%d", text, tw, th);
    tex = texture_cached(key);
    if (!tex) {
      rgba = paint_marking(text, tw, th, p->marking != NULL);
      tex = texture_put(key, tw, th, rgba, false);
    }
    inset = 0.92f;
  }
  int id = add_node(m, node, "");
  Node *n = &m->nodes[id];
  n->flags = NF_DECAL;
  n->mesh = mesh_plane_y(w * inset, d * inset, h / 2 + maxf(0.002f, h * 0.02f));
  n->nmats = 1;
  n->mats[0] = (Material){.mode = MAT_DECAL, .color = v3(1, 1, 1), .opacity = 1, .transparent = true, .tex = tex, .uv_scale = 1};
  const Node *parent = &m->nodes[node];
  if (parent->inst) {
    int c = arr_len(parent->inst);
    memcpy(arr_add(n->inst, c), parent->inst, sizeof(Mat4) * (size_t)c);
    n->inst_dirty = true;
  }
}

// ------------------------------------------------------------------ parts
static void tag_part(Node *n, const PartSpec *p, int layer, int part) {
  n->name = p->name;
  n->info = p->info ? p->info : "";
  n->category = p->category;
  n->detail = p->detail;
  n->specs = p->specs;
  n->material_name = p->material;
  n->flags |= NF_PART;
  n->layer_index = layer;
  n->part_index = part;
}

static Material part_material(const PartSpec *p, const BuildOpts *o, const char *fallback_mat) {
  Material mat = material_of(p->material ? p->material : fallback_mat, p->color, p->glow);
  if (p->finish && o->paint) {
    mat.tex = finish_texture(p->finish, p->color);
    mat.uv_scale = 1 / FINISH_TILE;
    mat.color = v3(1, 1, 1);
  }
  return mat;
}

static void set_pulse(Node *n, const PartSpec *p) {
  n->has_pulse = true;
  n->pulse_phase = is_set(p->pulse_phase) ? (float)p->pulse_phase : 0;
  n->pulse_level = &p->pulse_level;
  n->pulse_color = hex_linear(p->pulse_color ? p->pulse_color : "#ff7a2f");
}

static void set_mats(Node *n, const PartSpec *p, const BuildOpts *o) {
  if (p->shape == SHAPE_PORT) {
    n->nmats = 2;
    n->mats[0] = part_material(p, o, "steel");
    n->mats[1] = material_of("plastic", "#101215", NULL);
  } else {
    n->nmats = 1;
    n->mats[0] = part_material(p, o, NULL);
  }
}

static Mesh *clone_mesh(const Mesh *src) {
  Mesh *m = mesh_new();
  memcpy(arr_add(m->v, arr_len(src->v)), src->v, sizeof(Vertex) * (size_t)arr_len(src->v));
  if (src->idx) memcpy(arr_add(m->idx, arr_len(src->idx)), src->idx, sizeof(u32) * (size_t)arr_len(src->idx));
  memcpy(m->groups, src->groups, sizeof m->groups);
  m->ngroups = src->ngroups;
  m->box = src->box;
  return m;
}

static Mesh *part_mesh(const PartSpec *p, float *h_out) {
  float h;
  Mesh *g;
  switch (p->shape) {
  case SHAPE_BOX: {
    float bw = p->has_size ? (float)p->size.x : 1, bh = p->has_size ? (float)p->size.y : 1, bd = p->has_size ? (float)p->size.z : 1;
    float r = minf(is_set(p->round) ? (float)p->round : 0, minf(bw / 2, minf(bh / 2, bd / 2)));
    g = r > 0 ? mesh_rounded_box(bw, bh, bd, 3, r) : mesh_box(bw, bh, bd);
    h = bh;
    break;
  }
  case SHAPE_FAN:
    h = is_set(p->height) ? (float)p->height : 1;
    g = mesh_fan(is_set(p->radius) ? (float)p->radius : 10, h, is_set(p->blades) ? (int)p->blades : 7, p->ring,
                 p->blade && !strcmp(p->blade, "prop"));
    if (p->mirror) mesh_mirror_x(g);
    break;
  case SHAPE_OUTLINE: {
    h = is_set(p->height) ? (float)p->height : 1;
    Vec2 *pts = NULL, *cs = NULL;
    float *rs = NULL;
    for (int i = 0; i < arr_len(p->points); i++) arr_push(pts, v2((float)p->points[i].x, (float)p->points[i].y));
    for (int i = 0; i < arr_len(p->cutouts); i++) {
      arr_push(cs, v2((float)p->cutouts[i].at.x, (float)p->cutouts[i].at.y));
      arr_push(rs, (float)p->cutouts[i].radius);
    }
    g = mesh_outline(pts, arr_len(pts), h, cs, rs, arr_len(cs));
    arr_free(pts);
    arr_free(cs);
    arr_free(rs);
    break;
  }
  case SHAPE_PORT: {
    float pw = p->has_size ? (float)p->size.x : 9, ph = p->has_size ? (float)p->size.y : 6, pd = p->has_size ? (float)p->size.z : 16;
    h = ph;
    g = mesh_port(p->port ? p->port : "dp", pw, ph, pd);
    if (is_set(p->facing) && p->facing != 0) mesh_transform(g, m4rot_y((float)p->facing * PI / 180));
    break;
  }
  case SHAPE_SPHERE: {
    float r = is_set(p->radius) ? (float)p->radius : 0.5f;
    h = r * 2;
    g = mesh_sphere(r, 16, 12);
    break;
  }
  default:
    h = is_set(p->height) ? (float)p->height : 1;
    g = mesh_cylinder(is_set(p->radius) ? (float)p->radius : 0.5f, h, 32);
  }
  *h_out = h;
  return g;
}

static void build_part(Model *m, int group, const PartSpec *p, int li, int pi, float slab_top, const Footprint *avoid, int navoid, const BuildOpts *o) {
  float x = p->has_at ? (float)p->at.x : 0, z = p->has_at ? (float)p->at.y : 0;
  float base = slab_top + (is_set(p->lift) ? (float)p->lift : 0);
  if (p->shape == SHAPE_FILE) {
    int id = add_node(m, group, p->name);
    tag_part(&m->nodes[id], p, li, pi);
    return;
  }
  float h;
  Mesh *geo = part_mesh(p, &h);
  int count = part_copies(p);
  bool spins = p->spin.set && (p->spin.is_rule || p->spin.num != 0);

  if (spins && p->repeat.on && p->repeat.has_around) {
    int id = add_node(m, group, p->name);
    Node *n = &m->nodes[id];
    tag_part(n, p, li, pi);
    n->mesh = geo;
    set_mats(n, p, o);
    for (int i = 0; i < count; i++) arr_push(n->inst, copy_matrix(p, i, count, 0, base + h / 2, 0, NULL, 0));
    n->inst_dirty = true;
    n->home = v3(x, 0, z);
    n->spin = &p->spin;
    n->names = p->repeat.names;
    if (p->has_pulse) set_pulse(n, p);
    return;
  }
  if (spins) {
    int gid = count == 1 ? -1 : add_node(m, group, p->name);
    if (gid >= 0) tag_part(&m->nodes[gid], p, li, pi);
    for (int i = 0; i < count; i++) {
      int id = add_node(m, gid >= 0 ? gid : group, NULL);
      Node *n = &m->nodes[id];
      Mat4 t = copy_matrix(p, i, count, x, base + h / 2, z, NULL, 0);
      n->home = v3(t.m[12], t.m[13], t.m[14]);
      n->rot0 = atan2f(t.m[8], t.m[0]);
      n->mesh = i == 0 ? geo : clone_mesh(geo);
      set_mats(n, p, o);
      n->spin = &p->spin;
      tag_part(n, p, li, pi);
      if (count > 1) {
        const char *copy_name = i < arr_len(p->repeat.names) ? p->repeat.names[i] : NULL;
        if (copy_name) n->name = copy_name;
        else {
          char b[160];
          snprintf(b, sizeof b, "%s %d", p->name, i + 1);
          n->name = arena_str(&m->ar, b);
        }
      }
    }
    return;
  }
  int id = add_node(m, group, p->name);
  Node *n = &m->nodes[id];
  tag_part(n, p, li, pi);
  n->mesh = geo;
  set_mats(n, p, o);
  if (p->has_pulse) set_pulse(n, p);
  if (count == 1) {
    n->home = v3(x, base + h / 2, z);
  } else {
    if (p->has_scatter) n->flags |= NF_SCATTER;
    for (int i = 0; i < count; i++) arr_push(n->inst, copy_matrix(p, i, count, x, base + h / 2, z, avoid, navoid));
    n->inst_dirty = true;
    n->names = p->repeat.names;
  }
  if (o->paint && !p->has_scatter) mark_chip(m, id, p, h);
}

// ------------------------------------------------------------------ buses on a layer
static const Footprint *print_named(const Footprint *f, int n, const char *name) {
  const Footprint *found = NULL;
  for (int i = 0; i < n; i++) // later entries win, as a map built in order would
    if (f[i].name && *f[i].name && !strcmp(f[i].name, name)) found = &f[i];
  return found;
}
static Vec2 edge_toward(const Footprint *fp, float tx, float tz) {
  float dx = tx - fp->x, dz = tz - fp->z;
  float k1 = dx != 0 ? fp->w / 2 / fabsf(dx) : INFINITY, k2 = dz != 0 ? fp->d / 2 / fabsf(dz) : INFINITY;
  float k = minf(k1, k2);
  if (!isfinite(k)) return v2(fp->x, fp->z);
  k = minf(k, 1);
  return v2(fp->x + dx * k, fp->z + dz * k);
}

static void build_link(Model *m, int group, const LinkSpec *l, const Footprint *prints, int nprints, float y) {
  const Footprint *src = print_named(prints, nprints, l->from);
  if (!src) return;
  int lanes = maxi(1, is_set(l->lanes) ? (int)l->lanes : 6);
  float width = is_set(l->width) ? (float)l->width : 0.4f;
  float gap = width * 2.2f;
  Mesh *g = mesh_new();
  int pathno = 0;
  for (int t = 0; t < arr_len(l->to); t++) {
    const Footprint *dst = print_named(prints, nprints, l->to[t]);
    if (!dst) continue;
    Vec2 a = edge_toward(src, dst->x, dst->z), b = edge_toward(dst, src->x, src->z);
    float len = hypotf(b.x - a.x, b.y - a.y);
    if (len == 0) len = 1;
    float nx = -(b.y - a.y) / len, nz = (b.x - a.x) / len;
    for (int k = 0; k < lanes; k++, pathno++) {
      float off = ((float)k - (float)(lanes - 1) / 2) * gap;
      Vec2 path[4];
      route45(a.x + nx * off, a.y + nz * off, b.x + nx * off, b.y + nz * off, 0.5f, path);
      float lengths[4] = {0};
      for (int i = 1; i < 4; i++) lengths[i] = lengths[i - 1] + hypotf(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
      float total = lengths[3] > 0 ? lengths[3] : 1;
      float lane = fmodf((float)pathno * 0.618f, 1);
      for (int i = 0; i < 3; i++) {
        float ax = path[i].x, az = path[i].y, bx = path[i + 1].x, bz = path[i + 1].y;
        float sl = hypotf(bx - ax, bz - az);
        if (sl == 0) continue;
        float sx = -(bz - az) / sl * width / 2, sz = (bx - ax) / sl * width / 2;
        u32 v = (u32)arr_len(g->v);
        float ta = lengths[i] / total, tb = lengths[i + 1] / total;
        arr_push(g->v, ((Vertex){ax + sx, y, az + sz, 0, 1, 0, 0, 0, ta, lane}));
        arr_push(g->v, ((Vertex){ax - sx, y, az - sz, 0, 1, 0, 0, 0, ta, lane}));
        arr_push(g->v, ((Vertex){bx + sx, y, bz + sz, 0, 1, 0, 0, 0, tb, lane}));
        arr_push(g->v, ((Vertex){bx - sx, y, bz - sz, 0, 1, 0, 0, 0, tb, lane}));
        u32 idx[6] = {v, v + 2, v + 1, v + 1, v + 2, v + 3};
        memcpy(arr_add(g->idx, 6), idx, sizeof idx);
      }
    }
  }
  if (!pathno) {
    mesh_free(g);
    return;
  }
  mesh_update_box(g);
  char fallback[160];
  const char *name = l->name;
  if (!name) {
    snprintf(fallback, sizeof fallback, "%s bus", l->from);
    name = arena_str(&m->ar, fallback);
  }
  int id = add_node(m, group, name);
  Node *n = &m->nodes[id];
  n->mesh = g;
  n->flags = NF_FLOW;
  n->info = l->info ? l->info : "";
  n->specs = l->specs;
  n->category = l->category ? l->category : (l->kind && strcmp(l->kind, "data") ? "Power path" : "Data bus");
  n->level = &l->level;
  n->nmats = 1;
  const char *col = l->color ? l->color : (l->kind ? wire_kind(l->kind)->glow : "#76b900");
  n->mats[0] = (Material){.mode = MAT_FLOW, .glow = hex_linear(col), .opacity = 1, .uv_scale = 1};
}

// ------------------------------------------------------------------ wires
static bool locate(const Model *m, const char *name, int *node_out, Box *box_out) {
  // In tree order: the first node with this name, or a row with a copy of this name.
  int n = arr_len(m->nodes);
  for (int i = 1; i < n; i++) {
    const Node *o = &m->nodes[i];
    if (o->flags & (NF_WIRE | NF_WIRES)) continue;
    int copy = -1;
    for (int c = 0; o->names && c < arr_len(o->names); c++)
      if (!strcmp(o->names[c], name)) {
        copy = c;
        break;
      }
    if (copy >= 0 && o->inst) {
      Box b = box_empty(), g = o->mesh->box;
      for (int k = 0; k < 8; k++) {
        Vec3 c = v3(k & 1 ? g.max.x : g.min.x, k & 2 ? g.max.y : g.min.y, k & 4 ? g.max.z : g.min.z);
        box_add(&b, m4point(o->inst[copy], c));
      }
      *node_out = i;
      *box_out = b;
      return true;
    }
    if (!strcmp(o->name, name)) {
      // The subtree's model-space box, in the node's own space.
      Mat4 inv = m4invert(o->world);
      Box b = box_empty(), g = o->box;
      for (int k = 0; k < 8; k++) {
        Vec3 c = v3(k & 1 ? g.max.x : g.min.x, k & 2 ? g.max.y : g.min.y, k & 4 ? g.max.z : g.min.z);
        box_add(&b, m4point(inv, c));
      }
      *node_out = i;
      *box_out = b;
      return true;
    }
  }
  return false;
}

static Vec3 box_clamp(Box b, Vec3 p) {
  return v3(clampf(p.x, b.min.x, b.max.x), clampf(p.y, b.min.y, b.max.y), clampf(p.z, b.min.z, b.max.z));
}

static void build_wires(Model *m, const AssemblySpec *spec) {
  // Boxes so far (everything but the wires).
  for (int i = arr_len(m->nodes) - 1; i >= 0; i--) {
    Node *n = &m->nodes[i];
    box_union(&n->box, n->self);
    if (n->parent >= 0 && !(n->flags & NF_DECAL)) box_union(&m->nodes[n->parent].box, n->box);
  }
  int wiring = add_node(m, m->root, "Wiring");
  m->wiring = wiring;
  m->nodes[wiring].flags = NF_WIRES;
  m->nodes[wiring].info = "Every wire between the parts, drawn with what flows in it.";
  for (int s = 0; s < arr_len(spec->wires); s++) {
    const WireSpec *w = &spec->wires[s];
    int from, to;
    Box bf, bt;
    bool ok_f = locate(m, w->from, &from, &bf), ok_t = locate(m, w->to, &to, &bt);
    if (!ok_f) arr_push(m->missing, w->from);
    if (!ok_t) arr_push(m->missing, w->to);
    if (!ok_f || !ok_t) continue;
    int nvia = arr_len(w->via);
    Vec3 cf = m4point(m->nodes[from].world, box_center(bf)), ct = m4point(m->nodes[to].world, box_center(bt));
    Vec3 first = nvia ? v3((float)w->via[0].x, (float)w->via[0].y, (float)w->via[0].z) : ct;
    Vec3 last = nvia ? v3((float)w->via[nvia - 1].x, (float)w->via[nvia - 1].y, (float)w->via[nvia - 1].z) : cf;
    WireState st = {0};
    st.a.node = from;
    st.a.local = box_clamp(bf, m4point(m4invert(m->nodes[from].world), first));
    st.b.node = to;
    st.b.local = box_clamp(bt, m4point(m4invert(m->nodes[to].world), last));
    st.a.p0 = m4point(m->nodes[from].world, st.a.local);
    st.b.p0 = m4point(m->nodes[to].world, st.b.local);
    st.a.last = st.a.p0, st.b.last = st.b.p0;
    Vec3 *pts = NULL;
    arr_push(pts, st.a.p0);
    if (nvia) {
      for (int k = 0; k < nvia; k++) arr_push(pts, v3((float)w->via[k].x, (float)w->via[k].y, (float)w->via[k].z));
    } else {
      Vec3 mid = v3lerp(st.a.p0, st.b.p0, 0.5f);
      mid.y += -0.06f * hypotf(st.b.p0.x - st.a.p0.x, st.b.p0.z - st.a.p0.z);
      arr_push(pts, mid);
    }
    arr_push(pts, st.b.p0);
    int npts = arr_len(pts);
    float total = 0;
    for (int i = 1; i < npts; i++) total += v3dist(pts[i], pts[i - 1]);
    if (total == 0) total = 1;
    float run = 0;
    for (int i = 1; i < npts - 1; i++) {
      run += v3dist(pts[i], pts[i - 1]);
      arr_push(st.inner, pts[i]);
      arr_push(st.inner_t, run / total);
    }
    float length = curve_length(pts, npts);
    st.segments = mini(240, maxi(24, (int)lroundf(length / 1.2f)));
    st.radius = (is_set(w->gauge) ? (float)w->gauge : 1) / 2;
    st.spec = s;
    int id = add_node(m, wiring, w->name);
    Node *n = &m->nodes[id];
    n->flags = NF_WIRE;
    n->info = w->info ? w->info : "";
    n->specs = w->specs;
    const WireKind *k = wire_kind(w->kind);
    char cat[64];
    snprintf(cat, sizeof cat, "Wire \xC2\xB7 %s", k->label);
    n->category = w->category ? w->category : arena_str(&m->ar, cat);
    n->level = &w->level;
    n->mesh = mesh_new();
    mesh_tube(n->mesh, pts, npts, st.segments, st.radius);
    n->nmats = 1;
    n->mats[0] = (Material){.mode = MAT_WIRE,
                            .color = hex_linear(w->color ? w->color : k->color),
                            .metalness = 0,
                            .roughness = 0.42f,
                            .opacity = 1,
                            .clearcoat = 0.3f,
                            .uv_scale = 1,
                            .glow = hex_linear(k->glow),
                            .wire_mode = k->mode,
                            .wire_phase = is_set(w->phase) ? (float)w->phase : 0,
                            .wire_length = length};
    n->wire = arr_len(m->wires);
    arr_push(m->wires, st);
    arr_free(pts);
    m->nodes[id].self = m->nodes[id].mesh->box;
    m->nodes[id].box = m->nodes[id].mesh->box;
    box_union(&m->nodes[wiring].box, m->nodes[id].box);
  }
  box_union(&m->nodes[m->root].box, m->nodes[wiring].box);
}

static void relay_wires(Model *m) {
  for (int i = 0; i < arr_len(m->nodes); i++) {
    Node *n = &m->nodes[i];
    if (n->wire < 0) continue;
    WireState *s = &m->wires[n->wire];
    Vec3 a = m4point(m->nodes[s->a.node].world, s->a.local);
    Vec3 b = m4point(m->nodes[s->b.node].world, s->b.local);
    if (v3dist(a, s->a.last) < 1e-3f && v3dist(b, s->b.last) < 1e-3f) continue;
    s->a.last = a, s->b.last = b;
    Vec3 da = v3sub(a, s->a.p0), db = v3sub(b, s->b.p0);
    Vec3 *pts = NULL;
    arr_push(pts, a);
    for (int k = 0; k < arr_len(s->inner); k++)
      arr_push(pts, v3add(s->inner[k], v3lerp(da, db, s->inner_t[k])));
    arr_push(pts, b);
    mesh_tube(n->mesh, pts, arr_len(pts), s->segments, s->radius);
    arr_free(pts);
  }
}

// ------------------------------------------------------------------ build
Model *model_build(AssemblySpec *spec, BuildOpts opts) {
  Model *m = calloc(1, sizeof *m);
  m->spec = spec;
  m->layered = true;
  m->flow_speed = 1;
  m->wiring = -1;
  m->root = add_node(m, -1, spec->name);
  m->nodes[m->root].flags = NF_ROOT;
  float w = (float)spec->footprint.x, d = (float)spec->footprint.y, y = 0;
  for (int li = 0; li < arr_len(spec->layers); li++) {
    const LayerSpec *l = &spec->layers[li];
    if (is_set(l->base)) y = (float)l->base;
    int group = add_node(m, m->root, l->name);
    Node *gn = &m->nodes[group];
    gn->flags = NF_LAYER;
    gn->info = l->info ? l->info : "";
    gn->category = l->category ? l->category : "Layer";
    gn->specs = l->specs;
    gn->material_name = l->material;
    gn->detail = l->detail;
    gn->home = v3(0, y, 0);
    gn->layer_index = li;
    arr_push(m->pieces, group);
    float t = (float)l->thickness;
    if (t > 0) {
      int slab = add_node(m, group, NULL);
      Node *n = &m->nodes[slab];
      n->flags = NF_SLAB;
      n->layer_index = li;
      n->home = v3(0, t / 2, 0);
      Material mat = material_of(l->material, l->color, l->glow);
      if (arr_len(l->holes)) {
        Vec2 *cs = NULL;
        float *rs = NULL;
        for (int i = 0; i < arr_len(l->holes); i++) {
          arr_push(cs, v2((float)l->holes[i].at.x, (float)l->holes[i].at.y));
          arr_push(rs, (float)l->holes[i].radius);
        }
        n->mesh = mesh_holed_slab(w, d, t, cs, rs, arr_len(cs));
        arr_free(cs);
        arr_free(rs);
        n->nmats = 1;
        n->mats[0] = mat;
      } else {
        n->mesh = mesh_box(w, t, d);
        n->nmats = 6;
        for (int f = 0; f < 6; f++) n->mats[f] = mat;
        if (l->surface && opts.paint) {
          int seed = 0;
          for (const unsigned char *c = (const unsigned char *)l->name; *c; c++) seed += (*c & 0xC0) != 0x80;
          n->mats[2].tex = slab_texture(l, w, d, true, (u32)seed);
          n->mats[3].tex = slab_texture(l, w, d, false, (u32)seed + 101);
          n->mats[2].color = n->mats[3].color = v3(1, 1, 1);
        }
      }
    }
    Footprint *prints = layer_footprints(l);
    int np = arr_len(prints);
    for (int k = 0; k < arr_len(l->parts); k++) build_part(m, group, &l->parts[k], li, k, t, prints, np, &opts);
    for (int k = 0; k < arr_len(l->links); k++) build_link(m, group, &l->links[k], prints, np, t + 0.03f);
    arr_free(prints);

    // The next layer starts above the tallest thing on this one, not just above the slab.
    update_worlds(m);
    float top = -INFINITY;
    for (int i = group + 1; i < arr_len(m->nodes); i++) {
      Node *n = &m->nodes[i];
      n->self = self_box(n);
      if (!box_is_empty(n->self)) top = maxf(top, n->self.max.y);
    }
    top = isfinite(top) ? top - y : 0;
    y += maxf(t, top);
  }
  update_worlds(m);
  for (int i = 0; i < arr_len(m->nodes); i++) m->nodes[i].self = self_box(&m->nodes[i]);
  if (arr_len(spec->wires)) {
    build_wires(m, spec);
  } else {
    for (int i = arr_len(m->nodes) - 1; i >= 0; i--) {
      Node *n = &m->nodes[i];
      box_union(&n->box, n->self);
      if (n->parent >= 0 && !(n->flags & NF_DECAL)) box_union(&m->nodes[n->parent].box, n->box);
    }
  }
  m->box = m->nodes[m->root].box;
  if (box_is_empty(m->box)) m->box = (Box){v3(-1, -1, -1), v3(1, 1, 1)};
  m->center = box_center(m->box);
  m->radius = maxf(box_radius(m->box), 1e-6f);

  // Disassemble: rank the pieces along y and open equal gaps about the stack's middle.
  int np = arr_len(m->pieces);
  arr_set_len(m->spread_dir, np * 3);
  arr_set_len(m->room_to, np);
  memset(m->room_to, 0, sizeof(Vec3) * (size_t)np);
  int *order = malloc(sizeof(int) * (size_t)maxi(np, 1));
  float *along = malloc(sizeof(float) * (size_t)maxi(np, 1));
  for (int i = 0; i < np; i++) {
    order[i] = i;
    Box b = m->nodes[m->pieces[i]].box;
    along[i] = box_is_empty(b) ? m->nodes[m->pieces[i]].home.y : box_center(b).y;
  }
  for (int i = 1; i < np; i++) { // stable: equal centres keep their order
    int o = order[i], j = i;
    while (j > 0 && along[order[j - 1]] > along[o]) order[j] = order[j - 1], j--;
    order[j] = o;
  }
  float mid = (float)(np - 1) / 2;
  m->spread_gap = np > 1 ? m->radius / (float)(np - 1) : 0;
  for (int r = 0; r < np; r++) {
    int i = order[r];
    m->spread_dir[i * 3] = 0;
    m->spread_dir[i * 3 + 1] = (float)r - mid;
    m->spread_dir[i * 3 + 2] = 0;
  }
  free(order);
  free(along);
  model_start_values(m, NULL, 0);
  model_update(m);
  return m;
}

void model_free(Model *m) {
  if (!m) return;
  for (int i = 0; i < arr_len(m->nodes); i++) {
    mesh_free(m->nodes[i].mesh);
    arr_free(m->nodes[i].inst);
    arr_free(m->nodes[i].inst_tint);
  }
  for (int i = 0; i < arr_len(m->wires); i++) {
    arr_free(m->wires[i].inner);
    arr_free(m->wires[i].inner_t);
  }
  arr_free(m->nodes);
  arr_free(m->wires);
  arr_free(m->pieces);
  arr_free(m->spread_dir);
  arr_free(m->room_to);
  arr_free(m->values);
  arr_free(m->missing);
  arena_free(&m->ar);
  free(m);
}

void model_update(Model *m) {
  update_worlds(m);
  relay_wires(m);
}

void model_reset_anim(Model *m) {
  for (int i = 0; i < arr_len(m->nodes); i++) {
    Node *n = &m->nodes[i];
    n->spin_angle = 0;
    n->flow_a = n->flow_b = 0;
  }
}

void model_tick(Model *m, float dt) {
  float speed = m->flow_speed;
  int nv = arr_len(m->values);
  for (int i = 0; i < arr_len(m->nodes); i++) {
    Node *n = &m->nodes[i];
    if (n->spin) {
      float rpm = (float)value_eval(n->spin, m->values, nv, 0);
      n->spin_angle += rpm / 60 / SPIN_SLOWDOWN * TAU * dt;
      if (n->spin_angle > TAU * 64 || n->spin_angle < -TAU * 64) n->spin_angle = fmodf(n->spin_angle, TAU);
    }
    if (n->flags & NF_FLOW) {
      float l = (float)value_eval(n->level, m->values, nv, 1);
      n->level_now = l;
      n->flow_a += speed * (0.25f + 0.75f * l) * 0.6f * dt;
    } else if (n->flags & NF_WIRE) {
      float l = (float)value_eval(n->level, m->values, nv, 1);
      n->level_now = l;
      switch (n->mats[0].wire_mode) {
      case 0: n->flow_a += speed * (25 + 160 * l) * dt; break;
      case 1:
        n->flow_a += speed * TAU * (0.5f + 3 * l) * dt;
        n->flow_b += speed * 70 * dt;
        break;
      case 2: n->flow_a += speed * 90 * dt; break;
      default: n->flow_a += speed * 160 * dt;
      }
    }
    if (n->has_pulse) {
      float l = (float)value_eval(n->pulse_level, m->values, nv, 1);
      n->flow_a += speed * TAU * (0.5f + 3 * l) * dt;
      float w = sinf(n->flow_a + n->pulse_phase * TAU / 3);
      for (int k = 0; k < n->nmats; k++) {
        n->mats[k].emissive = n->pulse_color;
        n->mats[k].emissive_intensity = 2.4f * l * fabsf(w);
      }
    }
  }
}

void model_set_value(Model *m, const char *id, double v) {
  for (int i = 0; i < arr_len(m->values); i++)
    if (!strcmp(m->values[i].id, id)) m->values[i].value = v;
}
double model_get_value(const Model *m, const char *id) {
  for (int i = 0; i < arr_len(m->values); i++)
    if (!strcmp(m->values[i].id, id)) return m->values[i].value;
  return 0;
}
void model_start_values(Model *m, const ControlValue *keep, int n) {
  arr_clear(m->values);
  for (int i = 0; i < arr_len(m->spec->controls); i++) {
    const ControlSpec *c = &m->spec->controls[i];
    double mn = is_set(c->min) ? c->min : 0, mx = is_set(c->max) ? c->max : 100;
    double v = is_set(c->value) ? c->value : mn;
    for (int k = 0; k < n; k++)
      if (!strcmp(keep[k].id, c->id)) v = keep[k].value;
    v = v < mn ? mn : v > mx ? mx : v;
    arr_push(m->values, ((ControlValue){c->id ? c->id : "", v}));
  }
  // Rules that follow controls of the file above, carried in from a dive.
  for (int k = 0; k < n; k++) {
    bool have = false;
    for (int i = 0; i < arr_len(m->values); i++) have = have || !strcmp(m->values[i].id, keep[k].id);
    if (!have) arr_push(m->values, keep[k]);
  }
  int nv = arr_len(m->values);
  for (int i = 0; i < arr_len(m->nodes); i++) {
    Node *o = &m->nodes[i];
    if (o->flags & (NF_FLOW | NF_WIRE)) o->level_now = (float)value_eval(o->level, m->values, nv, 1);
  }
}

// ------------------------------------------------------------------ apart and back
static void apply_spread(Model *m) {
  float amount = m->explode;
  for (int i = 0; i < arr_len(m->pieces); i++) {
    Node *n = &m->nodes[m->pieces[i]];
    n->off_spread = v3(0, m->spread_dir[i * 3 + 1] * m->spread_gap * amount, 0);
  }
}
void model_set_explode(Model *m, float amount) {
  m->explode = amount;
  apply_spread(m);
}

int node_top_piece(const Model *m, int node) {
  while (node >= 0) {
    for (int i = 0; i < arr_len(m->pieces); i++)
      if (m->pieces[i] == node) return i;
    node = m->nodes[node].parent;
  }
  return -1;
}

void model_make_room(Model *m, int node) {
  int np = arr_len(m->pieces);
  int top = node >= 0 ? node_top_piece(m, node) : -1;
  // Bottom to top, by assembled position.
  int *order = malloc(sizeof(int) * (size_t)maxi(np, 1));
  for (int i = 0; i < np; i++) order[i] = i;
  for (int i = 1; i < np; i++) {
    int o = order[i], j = i;
    while (j > 0 && m->nodes[m->pieces[order[j - 1]]].box.min.y > m->nodes[m->pieces[o]].box.min.y) order[j] = order[j - 1], j--;
    order[j] = o;
  }
  int k = -1;
  for (int i = 0; i < np; i++)
    if (order[i] == top) k = i;
  for (int r = 0; r < np; r++) {
    int i = order[r];
    Vec3 to = v3(0, 0, 0);
    if (top >= 0 && i != top) {
      float share = r == k - 1 ? 0.12f : 0.35f;
      to.y = (r > k ? 1.f : -1.f) * m->radius * share;
    }
    m->room_to[i] = to;
  }
  free(order);
  m->room_moving = true;
}

bool model_room_tick(Model *m, float dt) {
  if (!m->room_moving) return false;
  bool still = true;
  float k = 1 - expf(-7.67f * dt); // the same glide at any frame rate
  for (int i = 0; i < arr_len(m->pieces); i++) {
    Node *n = &m->nodes[m->pieces[i]];
    n->off_room = v3lerp(n->off_room, m->room_to[i], k);
    if (v3dist(n->off_room, m->room_to[i]) > 1e-3f * m->radius) still = false;
    else n->off_room = m->room_to[i];
  }
  if (still) m->room_moving = false;
  return !still;
}

static Vec3 chain_offset(const Model *m, int node) {
  Vec3 d = v3(0, 0, 0);
  for (int i = node; i >= 0; i = m->nodes[i].parent) {
    const Node *n = &m->nodes[i];
    d = v3add(d, v3add(v3add(n->off_spread, n->off_room), n->off_anim));
  }
  return d;
}

Box node_box_now(const Model *m, int node) {
  const Node *n = &m->nodes[node];
  if (n->flags & NF_WIRE) return n->mesh->box;
  return box_shift(n->box, chain_offset(m, node));
}

Box node_copy_box_now(const Model *m, int node, int copy) {
  const Node *n = &m->nodes[node];
  if (copy < 0 || !n->inst || copy >= arr_len(n->inst)) return node_box_now(m, node);
  Mat4 w = m4mul(n->world, n->inst[copy]);
  Box b = box_empty(), g = n->mesh->box;
  for (int k = 0; k < 8; k++) box_add(&b, m4point(w, v3(k & 1 ? g.max.x : g.min.x, k & 2 ? g.max.y : g.min.y, k & 4 ? g.max.z : g.min.z)));
  return b;
}

int model_find(const Model *m, const char *name) {
  for (int i = 1; i < arr_len(m->nodes); i++)
    if (m->nodes[i].name[0] && !strcmp(m->nodes[i].name, name)) return i;
  return -1;
}

int model_first_detail(const Model *m) {
  for (int i = 1; i < arr_len(m->nodes); i++)
    if (m->nodes[i].detail && m->nodes[i].detail[0]) return i;
  return -1;
}

// ------------------------------------------------------------------ picking
static bool ray_box(Vec3 o, Vec3 d, Box b, float tmax) {
  float t0 = 0, t1 = tmax;
  float os[3] = {o.x, o.y, o.z}, ds[3] = {d.x, d.y, d.z};
  float mn[3] = {b.min.x, b.min.y, b.min.z}, mx[3] = {b.max.x, b.max.y, b.max.z};
  for (int i = 0; i < 3; i++) {
    float inv = 1 / ds[i], a = (mn[i] - os[i]) * inv, c = (mx[i] - os[i]) * inv;
    if (a > c) {
      float s = a;
      a = c;
      c = s;
    }
    t0 = maxf(t0, a);
    t1 = minf(t1, c);
    if (t0 > t1) return false;
  }
  return true;
}

static bool ray_tri(Vec3 o, Vec3 d, Vec3 a, Vec3 b, Vec3 c, float *t) {
  Vec3 e1 = v3sub(b, a), e2 = v3sub(c, a), p = v3cross(d, e2);
  float det = v3dot(e1, p);
  if (fabsf(det) < 1e-12f) return false;
  float inv = 1 / det;
  Vec3 s = v3sub(o, a);
  float u = v3dot(s, p) * inv;
  if (u < 0 || u > 1) return false;
  Vec3 q = v3cross(s, e1);
  float v = v3dot(d, q) * inv;
  if (v < 0 || u + v > 1) return false;
  float tt = v3dot(e2, q) * inv;
  if (tt <= 0) return false;
  *t = tt;
  return true;
}

static bool ray_mesh(const Mesh *g, Vec3 o, Vec3 d, float *best) {
  if (!ray_box(o, d, g->box, *best)) return false;
  bool hit = false;
  int n = g->idx ? arr_len(g->idx) : arr_len(g->v);
  for (int i = 0; i + 2 < n; i += 3) {
    const Vertex *a = &g->v[g->idx ? g->idx[i] : (u32)i], *b = &g->v[g->idx ? g->idx[i + 1] : (u32)i + 1], *c = &g->v[g->idx ? g->idx[i + 2] : (u32)i + 2];
    float t;
    if (ray_tri(o, d, v3(a->px, a->py, a->pz), v3(b->px, b->py, b->pz), v3(c->px, c->py, c->pz), &t) && t < *best) {
      *best = t;
      hit = true;
    }
  }
  return hit;
}

Hit model_pick(const Model *m, Vec3 origin, Vec3 dir) {
  Hit h = {-1, -1, INFINITY, {0, 0, 0}};
  float best = INFINITY;
  int hit_node = -1, hit_copy = -1;
  for (int i = 0; i < arr_len(m->nodes); i++) {
    const Node *n = &m->nodes[i];
    if (!n->mesh || (n->flags & (NF_DECAL | NF_HIDDEN)) || n->opacity < 0.05f) continue;
    Mat4 inv = m4invert(n->world);
    Vec3 lo = m4point(inv, origin), ld = m4dir(inv, dir);
    if (n->inst) {
      for (int c = 0; c < arr_len(n->inst); c++) {
        Mat4 ii = m4invert(n->inst[c]);
        if (ray_mesh(n->mesh, m4point(ii, lo), m4dir(ii, ld), &best)) hit_node = i, hit_copy = c;
      }
    } else if (ray_mesh(n->mesh, lo, ld, &best)) {
      hit_node = i, hit_copy = -1;
    }
  }
  if (hit_node < 0) return h;
  // What it belongs to: the nearest named object (a slab selects its whole layer).
  int part = hit_node;
  while (part > 0 && !m->nodes[part].name[0]) part = m->nodes[part].parent;
  if (part <= 0) return h;
  h.node = part;
  h.copy = part == hit_node && m->nodes[part].names ? hit_copy : -1;
  h.t = best;
  h.point = v3mad(origin, dir, best);
  return h;
}
