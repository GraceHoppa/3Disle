#include "mesh.h"

Mesh *mesh_new(void) {
  Mesh *m = calloc(1, sizeof *m);
  m->box = box_empty();
  m->gl_dirty = true;
  return m;
}

void mesh_free(Mesh *m) {
  if (!m) return;
  arr_free(m->v);
  arr_free(m->idx);
  free(m);
}

void mesh_update_box(Mesh *m) {
  m->box = box_empty();
  for (int i = 0; i < arr_len(m->v); i++) box_add(&m->box, v3(m->v[i].px, m->v[i].py, m->v[i].pz));
}

void mesh_transform(Mesh *m, Mat4 t) {
  for (int i = 0; i < arr_len(m->v); i++) {
    Vertex *v = &m->v[i];
    Vec3 p = m4point(t, v3(v->px, v->py, v->pz));
    Vec3 n = v3norm(m4dir(t, v3(v->nx, v->ny, v->nz)));
    v->px = p.x, v->py = p.y, v->pz = p.z;
    v->nx = n.x, v->ny = n.y, v->nz = n.z;
  }
  mesh_update_box(m);
}

void mesh_to_triangles(Mesh *m) {
  if (!m->idx) return;
  Vertex *out = NULL;
  int n = arr_len(m->idx);
  arr_reserve(out, n);
  for (int i = 0; i < n; i++) arr_push(out, m->v[m->idx[i]]);
  arr_free(m->v);
  arr_free(m->idx);
  m->v = out;
}

void mesh_flat_normals(Mesh *m) {
  for (int i = 0; i + 2 < arr_len(m->v); i += 3) {
    Vertex *a = &m->v[i], *b = a + 1, *c = a + 2;
    Vec3 pa = v3(a->px, a->py, a->pz), pb = v3(b->px, b->py, b->pz), pc = v3(c->px, c->py, c->pz);
    Vec3 n = v3norm(v3cross(v3sub(pc, pb), v3sub(pa, pb)));
    for (int k = 0; k < 3; k++) a[k].nx = n.x, a[k].ny = n.y, a[k].nz = n.z;
  }
}

void mesh_append(Mesh *dst, const Mesh *src) {
  int n = arr_len(src->v);
  if (n) memcpy(arr_add(dst->v, n), src->v, sizeof(Vertex) * (size_t)n);
  box_union(&dst->box, src->box);
}

void mesh_mirror_x(Mesh *m) {
  for (int i = 0; i < arr_len(m->v); i++) m->v[i].px = -m->v[i].px;
  if (m->idx) {
    for (int i = 0; i + 2 < arr_len(m->idx); i += 3) {
      u32 t = m->idx[i + 1];
      m->idx[i + 1] = m->idx[i + 2];
      m->idx[i + 2] = t;
    }
  } else {
    for (int i = 0; i + 2 < arr_len(m->v); i += 3) {
      Vertex t = m->v[i + 1];
      m->v[i + 1] = m->v[i + 2];
      m->v[i + 2] = t;
    }
    mesh_flat_normals(m);
  }
  mesh_update_box(m);
}

int mesh_tri_count(const Mesh *m) { return (m->idx ? arr_len(m->idx) : arr_len(m->v)) / 3; }

static void add_tri_idx(Mesh *m, u32 a, u32 b, u32 c) {
  arr_push(m->idx, a);
  arr_push(m->idx, b);
  arr_push(m->idx, c);
}

// One face of a box, as a grid; u and v run across it. Matches the corner order the painted
// surfaces expect: on +y the canvas top is at -z, on -y it is at +z.
static void box_face(Mesh *m, Vec3 origin, Vec3 du, Vec3 dv, Vec3 n, int gu, int gv) {
  u32 base = (u32)arr_len(m->v);
  for (int j = 0; j <= gv; j++)
    for (int i = 0; i <= gu; i++) {
      float s = (float)i / (float)gu, t = (float)j / (float)gv;
      Vec3 p = v3add(v3mad(origin, du, s), v3scale(dv, t));
      arr_push(m->v, ((Vertex){p.x, p.y, p.z, n.x, n.y, n.z, s, t, 0, 0}));
    }
  for (int j = 0; j < gv; j++)
    for (int i = 0; i < gu; i++) {
      u32 a = base + (u32)(j * (gu + 1) + i), b = a + 1, c = a + (u32)gu + 1, d = c + 1;
      Vec3 pa = v3(m->v[a].px, m->v[a].py, m->v[a].pz), pb = v3(m->v[b].px, m->v[b].py, m->v[b].pz);
      Vec3 pc = v3(m->v[c].px, m->v[c].py, m->v[c].pz);
      if (v3dot(v3cross(v3sub(pb, pa), v3sub(pc, pa)), n) >= 0) {
        add_tri_idx(m, a, b, c);
        add_tri_idx(m, b, d, c);
      } else {
        add_tri_idx(m, a, c, b);
        add_tri_idx(m, b, c, d);
      }
    }
}

static void add_face_group(Mesh *m, int start, int mat) {
  m->groups[m->ngroups++] = (MeshGroup){start, arr_len(m->idx) - start, mat};
}

Mesh *mesh_box(float w, float h, float d) {
  Mesh *m = mesh_new();
  float x = w / 2, y = h / 2, z = d / 2;
  int s = 0;
  box_face(m, v3(x, y, z), v3(0, 0, -d), v3(0, -h, 0), v3(1, 0, 0), 1, 1), add_face_group(m, s, 0), s = arr_len(m->idx);
  box_face(m, v3(-x, y, -z), v3(0, 0, d), v3(0, -h, 0), v3(-1, 0, 0), 1, 1), add_face_group(m, s, 1), s = arr_len(m->idx);
  box_face(m, v3(-x, y, -z), v3(w, 0, 0), v3(0, 0, d), v3(0, 1, 0), 1, 1), add_face_group(m, s, 2), s = arr_len(m->idx);
  box_face(m, v3(-x, -y, z), v3(w, 0, 0), v3(0, 0, -d), v3(0, -1, 0), 1, 1), add_face_group(m, s, 3), s = arr_len(m->idx);
  box_face(m, v3(-x, y, z), v3(w, 0, 0), v3(0, -h, 0), v3(0, 0, 1), 1, 1), add_face_group(m, s, 4), s = arr_len(m->idx);
  box_face(m, v3(x, y, -z), v3(-w, 0, 0), v3(0, -h, 0), v3(0, 0, -1), 1, 1), add_face_group(m, s, 5);
  mesh_update_box(m);
  return m;
}

Mesh *mesh_rounded_box(float w, float h, float d, int segments, float radius) {
  int seg = segments * 2 + 1;
  float hs = 0.5f / (float)seg;
  radius = minf(radius, minf(w / 2, minf(h / 2, d / 2)));
  Vec3 inner = v3(w / 2 - radius, h / 2 - radius, d / 2 - radius);
  Mesh *m = mesh_new();
  // A unit cube's six faces as grids, each point pushed out to the rounded surface.
  Vec3 faces[6][4] = {
      {{.5f, .5f, .5f}, {0, 0, -1}, {0, -1, 0}, {1, 0, 0}},  {{-.5f, .5f, -.5f}, {0, 0, 1}, {0, -1, 0}, {-1, 0, 0}},
      {{-.5f, .5f, -.5f}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  {{-.5f, -.5f, .5f}, {1, 0, 0}, {0, 0, -1}, {0, -1, 0}},
      {{-.5f, .5f, .5f}, {1, 0, 0}, {0, -1, 0}, {0, 0, 1}},  {{.5f, .5f, -.5f}, {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}},
  };
  for (int f = 0; f < 6; f++) box_face(m, faces[f][0], faces[f][1], faces[f][2], faces[f][3], seg, seg);
  for (int i = 0; i < arr_len(m->v); i++) {
    Vertex *v = &m->v[i];
    Vec3 p = v3(v->px, v->py, v->pz);
    Vec3 s = v3(p.x > 0 ? 1.f : -1.f, p.y > 0 ? 1.f : -1.f, p.z > 0 ? 1.f : -1.f);
    Vec3 n = v3norm(v3(p.x - s.x * hs, p.y - s.y * hs, p.z - s.z * hs));
    v->px = inner.x * s.x + n.x * radius;
    v->py = inner.y * s.y + n.y * radius;
    v->pz = inner.z * s.z + n.z * radius;
    v->nx = n.x, v->ny = n.y, v->nz = n.z;
  }
  mesh_update_box(m);
  return m;
}

Mesh *mesh_cylinder(float r, float height, int radial) {
  Mesh *m = mesh_new();
  float hh = height / 2;
  for (int j = 0; j <= 1; j++)
    for (int i = 0; i <= radial; i++) {
      float th = (float)i / (float)radial * TAU;
      float sn = sinf(th), cs = cosf(th);
      arr_push(m->v, ((Vertex){r * sn, j ? -hh : hh, r * cs, sn, 0, cs, (float)i / (float)radial, (float)j, 0, 0}));
    }
  for (int i = 0; i < radial; i++) {
    u32 a = (u32)i, b = (u32)(i + radial + 1), c = b + 1, d = a + 1;
    add_tri_idx(m, a, b, d);
    add_tri_idx(m, b, c, d);
  }
  for (int cap = 0; cap < 2; cap++) {
    float y = cap ? -hh : hh, ny = cap ? -1.f : 1.f;
    u32 centre = (u32)arr_len(m->v);
    arr_push(m->v, ((Vertex){0, y, 0, 0, ny, 0, 0.5f, 0.5f, 0, 0}));
    u32 ring = (u32)arr_len(m->v);
    for (int i = 0; i <= radial; i++) {
      float th = (float)i / (float)radial * TAU;
      float sn = sinf(th), cs = cosf(th);
      arr_push(m->v, ((Vertex){r * sn, y, r * cs, 0, ny, 0, sn * .5f + .5f, cs * .5f * ny + .5f, 0, 0}));
    }
    for (int i = 0; i < radial; i++) {
      if (cap) add_tri_idx(m, ring + (u32)i + 1, ring + (u32)i, centre);
      else add_tri_idx(m, ring + (u32)i, ring + (u32)i + 1, centre);
    }
  }
  mesh_update_box(m);
  return m;
}

Mesh *mesh_sphere(float r, int ws, int hs) {
  Mesh *m = mesh_new();
  for (int j = 0; j <= hs; j++) {
    float th = (float)j / (float)hs * PI;
    for (int i = 0; i <= ws; i++) {
      float ph = (float)i / (float)ws * TAU;
      Vec3 n = v3(-cosf(ph) * sinf(th), cosf(th), sinf(ph) * sinf(th));
      arr_push(m->v, ((Vertex){n.x * r, n.y * r, n.z * r, n.x, n.y, n.z, (float)i / (float)ws, (float)j / (float)hs, 0, 0}));
    }
  }
  for (int j = 0; j < hs; j++)
    for (int i = 0; i < ws; i++) {
      u32 a = (u32)(j * (ws + 1) + i + 1), b = (u32)(j * (ws + 1) + i), c = (u32)((j + 1) * (ws + 1) + i), d = c + 1;
      if (j) add_tri_idx(m, a, b, d);
      if (j != hs - 1) add_tri_idx(m, b, c, d);
    }
  mesh_update_box(m);
  return m;
}

Mesh *mesh_plane_y(float w, float d, float y) {
  Mesh *m = mesh_new();
  float x = w / 2, z = d / 2;
  arr_push(m->v, ((Vertex){-x, y, -z, 0, 1, 0, 0, 0, 0, 0}));
  arr_push(m->v, ((Vertex){x, y, -z, 0, 1, 0, 1, 0, 0, 0}));
  arr_push(m->v, ((Vertex){-x, y, z, 0, 1, 0, 0, 1, 0, 0}));
  arr_push(m->v, ((Vertex){x, y, z, 0, 1, 0, 1, 1, 0, 0}));
  add_tri_idx(m, 0, 2, 1);
  add_tri_idx(m, 1, 2, 3);
  mesh_update_box(m);
  return m;
}

Mesh *mesh_lathe(const Vec2 *pts, int n, int segments) {
  Mesh *m = mesh_new();
  for (int i = 0; i <= segments; i++) {
    float ph = (float)i / (float)segments * TAU;
    for (int j = 0; j < n; j++)
      arr_push(m->v, ((Vertex){pts[j].x * sinf(ph), pts[j].y, pts[j].x * cosf(ph), 0, 1, 0, 0, 0, 0, 0}));
  }
  for (int i = 0; i < segments; i++)
    for (int j = 0; j < n - 1; j++) {
      u32 a = (u32)(j + i * n), b = (u32)(j + (i + 1) * n), c = b + 1, d = a + 1;
      add_tri_idx(m, a, b, d);
      add_tri_idx(m, c, d, b);
    }
  mesh_update_box(m);
  return m;
}

// ------------------------------------------------------------------ extrude
static float poly_area(const Vec2 *p, int n) {
  float a = 0;
  for (int i = n - 1, q = 0; q < n; i = q++) a += p[i].x * p[q].y - p[q].x * p[i].y;
  return a * 0.5f;
}
static void poly_reverse(Vec2 *p, int n) {
  for (int i = 0; i < n / 2; i++) {
    Vec2 t = p[i];
    p[i] = p[n - 1 - i];
    p[n - 1 - i] = t;
  }
}

static Vec2 bevel_vec(Vec2 pt, Vec2 prev, Vec2 next) {
  float vpx = pt.x - prev.x, vpy = pt.y - prev.y, vnx = next.x - pt.x, vny = next.y - pt.y;
  float plen2 = vpx * vpx + vpy * vpy, shrink, tx, ty;
  float col = vpx * vny - vpy * vnx;
  if (fabsf(col) > 1e-7f) {
    float pl = sqrtf(plen2), nl = sqrtf(vnx * vnx + vny * vny);
    float psx = prev.x - vpy / pl, psy = prev.y + vpx / pl;
    float nsx = next.x - vny / nl, nsy = next.y + vnx / nl;
    float sf = ((nsx - psx) * vny - (nsy - psy) * vnx) / col;
    tx = psx + vpx * sf - pt.x;
    ty = psy + vpy * sf - pt.y;
    float l2 = tx * tx + ty * ty;
    if (l2 <= 2) return v2(tx, ty);
    shrink = sqrtf(l2 / 2);
  } else {
    bool same = false;
    if (vpx > 1e-7f) same = vnx > 1e-7f;
    else if (vpx < -1e-7f) same = vnx < -1e-7f;
    else same = (vpy > 0) == (vny > 0);
    if (same) {
      tx = -vpy, ty = vpx;
      shrink = sqrtf(plen2);
    } else {
      tx = vpx, ty = vpy;
      shrink = sqrtf(plen2 / 2);
    }
  }
  return v2(tx / shrink, ty / shrink);
}

static void push_tri(Mesh *m, Vec3 a, Vec3 b, Vec3 c, Vec2 ua, Vec2 ub, Vec2 uc) {
  arr_push(m->v, ((Vertex){a.x, a.y, a.z, 0, 0, 0, ua.x, ua.y, 0, 0}));
  arr_push(m->v, ((Vertex){b.x, b.y, b.z, 0, 0, 0, ub.x, ub.y, 0, 0}));
  arr_push(m->v, ((Vertex){c.x, c.y, c.z, 0, 0, 0, uc.x, uc.y, 0, 0}));
}

static void movements(const Vec2 *pts, int n, Vec2 *out) {
  for (int i = 0, j = n - 1, k = 1; i < n; i++, j++, k++) {
    if (j == n) j = 0;
    if (k == n) k = 0;
    out[i] = bevel_vec(pts[i], pts[j], pts[k]);
  }
}

Mesh *mesh_extrude(const Outline *src, float depth, float bt, float bs, int segs) {
  Mesh *m = mesh_new();
  // Own copies, wound the way the bevel expects: outer clockwise, holes counter-clockwise.
  Outline o = {0};
  int nouter = arr_len(src->outer);
  if (nouter < 3) return m;
  memcpy(arr_add(o.outer, nouter), src->outer, sizeof(Vec2) * (size_t)nouter);
  bool reverse = poly_area(o.outer, nouter) >= 0;
  if (reverse) poly_reverse(o.outer, nouter);
  for (int h = 0; h < arr_len(src->holes); h++) {
    Vec2 *copy = NULL;
    int n = arr_len(src->holes[h]);
    memcpy(arr_add(copy, n), src->holes[h], sizeof(Vec2) * (size_t)n);
    if (reverse && poly_area(copy, n) < 0) poly_reverse(copy, n);
    arr_push(o.holes, copy);
  }
  u32 *faces = earcut(&o);

  // Every loop's points and how each moves as the bevel widens.
  Vec2 *all = NULL, *moves = NULL;
  int *loop_start = NULL, *loop_len = NULL;
  for (int l = -1; l < arr_len(o.holes); l++) {
    Vec2 *pts = l < 0 ? o.outer : o.holes[l];
    int n = arr_len(pts);
    arr_push(loop_start, arr_len(all));
    arr_push(loop_len, n);
    memcpy(arr_add(all, n), pts, sizeof(Vec2) * (size_t)n);
    movements(pts, n, arr_add(moves, n));
  }
  int vlen = arr_len(all);

  // Layers from the back, through the body, to the front.
  int layers = 2 * segs + 2;
  Vec3 *grid = malloc(sizeof(Vec3) * (size_t)(vlen * layers));
  int L = 0;
  for (int b = 0; b < segs; b++, L++) {
    float t = (float)b / (float)segs, z = bt * cosf(t * PI / 2), k = bs * sinf(t * PI / 2);
    for (int i = 0; i < vlen; i++) grid[L * vlen + i] = v3(all[i].x + moves[i].x * k, all[i].y + moves[i].y * k, -z);
  }
  for (int s = 0; s <= 1; s++, L++)
    for (int i = 0; i < vlen; i++)
      grid[L * vlen + i] = v3(all[i].x + moves[i].x * bs, all[i].y + moves[i].y * bs, s ? depth : 0);
  for (int b = segs - 1; b >= 0; b--, L++) {
    float t = (float)b / (float)segs, z = bt * cosf(t * PI / 2), k = bs * sinf(t * PI / 2);
    for (int i = 0; i < vlen; i++)
      grid[L * vlen + i] = v3(all[i].x + moves[i].x * k, all[i].y + moves[i].y * k, depth + z);
  }

  // Lids. The outline's own coordinates are their UVs, in mm.
#define UVTOP(p) v2((p).x, (p).y)
  for (int i = 0; i + 2 < arr_len(faces); i += 3) {
    Vec3 a = grid[faces[i]], b = grid[faces[i + 1]], c = grid[faces[i + 2]];
    push_tri(m, c, b, a, UVTOP(c), UVTOP(b), UVTOP(a));
  }
  Vec3 *top = grid + (layers - 1) * vlen;
  for (int i = 0; i + 2 < arr_len(faces); i += 3) {
    Vec3 a = top[faces[i]], b = top[faces[i + 1]], c = top[faces[i + 2]];
    push_tri(m, a, b, c, UVTOP(a), UVTOP(b), UVTOP(c));
  }
  // Walls.
  for (int l = 0; l < arr_len(loop_len); l++) {
    int st = loop_start[l], n = loop_len[l];
    for (int i = n - 1; i >= 0; i--) {
      int j = i, k = i - 1 < 0 ? n - 1 : i - 1;
      for (int s = 0; s < layers - 1; s++) {
        Vec3 a = grid[s * vlen + st + j], b = grid[s * vlen + st + k];
        Vec3 c = grid[(s + 1) * vlen + st + k], d = grid[(s + 1) * vlen + st + j];
        bool along_x = fabsf(a.y - b.y) < fabsf(a.x - b.x);
        Vec2 ua = v2(along_x ? a.x : a.y, 1 - a.z), ub = v2(along_x ? b.x : b.y, 1 - b.z);
        Vec2 uc = v2(along_x ? c.x : c.y, 1 - c.z), ud = v2(along_x ? d.x : d.y, 1 - d.z);
        push_tri(m, a, b, d, ua, ub, ud);
        push_tri(m, b, c, d, ub, uc, ud);
      }
    }
  }
  mesh_flat_normals(m);
  mesh_update_box(m);

  free(grid);
  arr_free(faces);
  arr_free(all);
  arr_free(moves);
  arr_free(loop_start);
  arr_free(loop_len);
  outline_free(&o);
  return m;
}

void outline_free(Outline *o) {
  arr_free(o->outer);
  for (int i = 0; i < arr_len(o->holes); i++) arr_free(o->holes[i]);
  arr_free(o->holes);
}

// ------------------------------------------------------------------ ear clipping with holes
typedef struct EN {
  int i;
  float x, y;
  struct EN *prev, *next;
  bool steiner;
} EN;

static float tri_area(const EN *p, const EN *q, const EN *r) {
  return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y);
}
static bool same_pt(const EN *a, const EN *b) { return a->x == b->x && a->y == b->y; }
static bool in_tri(float ax, float ay, float bx, float by, float cx, float cy, float px, float py) {
  return (cx - px) * (ay - py) >= (ax - px) * (cy - py) && (ax - px) * (by - py) >= (bx - px) * (ay - py) &&
         (bx - px) * (cy - py) >= (cx - px) * (by - py);
}
static EN *en_insert(EN ***pool, int i, float x, float y, EN *last) {
  EN *n = calloc(1, sizeof(EN));
  arr_push(*pool, n);
  n->i = i, n->x = x, n->y = y;
  if (!last) {
    n->prev = n->next = n;
  } else {
    n->next = last->next;
    n->prev = last;
    last->next->prev = n;
    last->next = n;
  }
  return n;
}
static void en_remove(EN *p) {
  p->next->prev = p->prev;
  p->prev->next = p->next;
}
static EN *linked(EN ***pool, const Vec2 *pts, int n, int base, bool clockwise) {
  EN *last = NULL;
  float sum = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) sum += (pts[j].x - pts[i].x) * (pts[i].y + pts[j].y);
  if (clockwise == (sum > 0)) {
    for (int i = 0; i < n; i++) last = en_insert(pool, base + i, pts[i].x, pts[i].y, last);
  } else {
    for (int i = n - 1; i >= 0; i--) last = en_insert(pool, base + i, pts[i].x, pts[i].y, last);
  }
  if (last && same_pt(last, last->next)) {
    en_remove(last);
    last = last->next;
  }
  return last;
}
static EN *filter_points(EN *start, EN *end) {
  if (!start) return start;
  if (!end) end = start;
  EN *p = start;
  bool again;
  do {
    again = false;
    if (!p->steiner && (same_pt(p, p->next) || tri_area(p->prev, p, p->next) == 0)) {
      en_remove(p);
      p = end = p->prev;
      if (p == p->next) break;
      again = true;
    } else {
      p = p->next;
    }
  } while (again || p != end);
  return end;
}
static bool is_ear(const EN *ear) {
  const EN *a = ear->prev, *b = ear, *c = ear->next;
  if (tri_area(a, b, c) >= 0) return false;
  for (const EN *p = c->next; p != a; p = p->next)
    if (in_tri(a->x, a->y, b->x, b->y, c->x, c->y, p->x, p->y) && tri_area(p->prev, p, p->next) >= 0) return false;
  return true;
}
static int sgn(float v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static bool on_seg(const EN *p, const EN *q, const EN *r) {
  return q->x <= maxf(p->x, r->x) && q->x >= minf(p->x, r->x) && q->y <= maxf(p->y, r->y) && q->y >= minf(p->y, r->y);
}
static bool intersects(const EN *p1, const EN *q1, const EN *p2, const EN *q2) {
  int o1 = sgn(tri_area(p1, q1, p2)), o2 = sgn(tri_area(p1, q1, q2)), o3 = sgn(tri_area(p2, q2, p1)), o4 = sgn(tri_area(p2, q2, q1));
  if (o1 != o2 && o3 != o4) return true;
  if (o1 == 0 && on_seg(p1, p2, q1)) return true;
  if (o2 == 0 && on_seg(p1, q2, q1)) return true;
  if (o3 == 0 && on_seg(p2, p1, q2)) return true;
  if (o4 == 0 && on_seg(p2, q1, q2)) return true;
  return false;
}
static bool intersects_poly(const EN *a, const EN *b) {
  const EN *p = a;
  do {
    if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i && intersects(p, p->next, a, b)) return true;
    p = p->next;
  } while (p != a);
  return false;
}
static bool locally_inside(const EN *a, const EN *b) {
  return tri_area(a->prev, a, a->next) < 0 ? tri_area(a, b, a->next) >= 0 && tri_area(a, a->prev, b) >= 0
                                           : tri_area(a, b, a->prev) < 0 || tri_area(a, a->next, b) < 0;
}
static bool middle_inside(const EN *a, const EN *b) {
  const EN *p = a;
  bool inside = false;
  float px = (a->x + b->x) / 2, py = (a->y + b->y) / 2;
  do {
    if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y &&
        (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x))
      inside = !inside;
    p = p->next;
  } while (p != a);
  return inside;
}
static bool valid_diagonal(const EN *a, const EN *b) {
  return a->next->i != b->i && a->prev->i != b->i && !intersects_poly(a, b) &&
         ((locally_inside(a, b) && locally_inside(b, a) && middle_inside(a, b) &&
           (tri_area(a->prev, a, b->prev) != 0 || tri_area(a, b->prev, b) != 0)) ||
          (same_pt(a, b) && tri_area(a->prev, a, a->next) > 0 && tri_area(b->prev, b, b->next) > 0));
}
static EN *split_polygon(EN ***pool, EN *a, EN *b) {
  EN *a2 = en_insert(pool, a->i, a->x, a->y, NULL), *b2 = en_insert(pool, b->i, b->x, b->y, NULL);
  EN *an = a->next, *bp = b->prev;
  a->next = b;
  b->prev = a;
  a2->next = an;
  an->prev = a2;
  b2->next = a2;
  a2->prev = b2;
  bp->next = b2;
  b2->prev = bp;
  return b2;
}
static void earcut_linked(EN ***pool, EN *ear, u32 **out, int pass);
static EN *cure_local(EN *start, u32 **out) {
  EN *p = start;
  do {
    EN *a = p->prev, *b = p->next->next;
    if (!same_pt(a, b) && intersects(a, p, p->next, b) && locally_inside(a, b) && locally_inside(b, a)) {
      arr_push(*out, (u32)a->i);
      arr_push(*out, (u32)p->i);
      arr_push(*out, (u32)b->i);
      en_remove(p);
      en_remove(p->next);
      p = start = b;
    }
    p = p->next;
  } while (p != start);
  return filter_points(p, NULL);
}
static void split_earcut(EN ***pool, EN *start, u32 **out) {
  EN *a = start;
  do {
    EN *b = a->next->next;
    while (b != a->prev) {
      if (a->i != b->i && valid_diagonal(a, b)) {
        EN *c = split_polygon(pool, a, b);
        a = filter_points(a, a->next);
        c = filter_points(c, c->next);
        earcut_linked(pool, a, out, 0);
        earcut_linked(pool, c, out, 0);
        return;
      }
      b = b->next;
    }
    a = a->next;
  } while (a != start);
}
static void earcut_linked(EN ***pool, EN *ear, u32 **out, int pass) {
  if (!ear) return;
  EN *stop = ear;
  while (ear->prev != ear->next) {
    EN *prev = ear->prev, *next = ear->next;
    if (is_ear(ear)) {
      arr_push(*out, (u32)prev->i);
      arr_push(*out, (u32)ear->i);
      arr_push(*out, (u32)next->i);
      en_remove(ear);
      ear = next->next;
      stop = next->next;
      continue;
    }
    ear = next;
    if (ear == stop) {
      if (!pass) earcut_linked(pool, filter_points(ear, NULL), out, 1);
      else if (pass == 1) {
        ear = cure_local(filter_points(ear, NULL), out);
        earcut_linked(pool, ear, out, 2);
      } else if (pass == 2) {
        split_earcut(pool, ear, out);
      }
      break;
    }
  }
}
static EN *leftmost(EN *start) {
  EN *p = start, *l = start;
  do {
    if (p->x < l->x || (p->x == l->x && p->y < l->y)) l = p;
    p = p->next;
  } while (p != start);
  return l;
}
static bool sector_contains(const EN *m, const EN *p) {
  return tri_area(m->prev, m, p->prev) < 0 && tri_area(p->next, m, m->next) < 0;
}
static EN *find_bridge(EN *hole, EN *outer) {
  EN *p = outer;
  float hx = hole->x, hy = hole->y, qx = -INFINITY;
  EN *m = NULL;
  if (same_pt(hole, p)) return p;
  do {
    if (same_pt(hole, p->next)) return p->next;
    if (hy <= p->y && hy >= p->next->y && p->next->y != p->y) {
      float x = p->x + (hy - p->y) * (p->next->x - p->x) / (p->next->y - p->y);
      if (x <= hx && x > qx) {
        qx = x;
        m = p->x < p->next->x ? p : p->next;
        if (x == hx) return m;
      }
    }
    p = p->next;
  } while (p != outer);
  if (!m) return NULL;
  const EN *stop = m;
  float mx = m->x, my = m->y, tan_min = INFINITY;
  p = m;
  do {
    if (hx >= p->x && p->x >= mx && hx != p->x &&
        in_tri(hy < my ? hx : qx, hy, mx, my, hy < my ? qx : hx, hy, p->x, p->y)) {
      float tn = fabsf(hy - p->y) / (hx - p->x);
      if (locally_inside(p, hole) && (tn < tan_min || (tn == tan_min && (p->x > m->x || (p->x == m->x && sector_contains(m, p)))))) {
        m = p;
        tan_min = tn;
      }
    }
    p = p->next;
  } while (p != stop);
  return m;
}
static int cmp_en_x(const void *a, const void *b) {
  const EN *x = *(EN *const *)a, *y = *(EN *const *)b;
  return x->x < y->x ? -1 : x->x > y->x ? 1 : x->y < y->y ? -1 : x->y > y->y;
}

u32 *earcut(const Outline *o) {
  u32 *out = NULL;
  EN **pool = NULL;
  int base = arr_len(o->outer);
  EN *outer = linked(&pool, o->outer, base, 0, true);
  if (outer && outer->next != outer->prev) {
    EN **queue = NULL;
    for (int h = 0; h < arr_len(o->holes); h++) {
      int n = arr_len(o->holes[h]);
      EN *list = linked(&pool, o->holes[h], n, base, false);
      base += n;
      if (!list) continue;
      if (list == list->next) list->steiner = true;
      arr_push(queue, leftmost(list));
    }
    if (queue) qsort(queue, (size_t)arr_len(queue), sizeof(EN *), cmp_en_x);
    for (int i = 0; i < arr_len(queue); i++) {
      EN *bridge = find_bridge(queue[i], outer);
      if (!bridge) continue;
      EN *rev = split_polygon(&pool, bridge, queue[i]);
      filter_points(rev, rev->next);
      outer = filter_points(bridge, bridge->next);
    }
    arr_free(queue);
    earcut_linked(&pool, outer, &out, 0);
  }
  for (int i = 0; i < arr_len(pool); i++) free(pool[i]);
  arr_free(pool);
  return out;
}

// ------------------------------------------------------------------ shapes built from those
static void arc_points(Vec2 **out, float cx, float cy, float r, float a0, float da, int res) {
  for (int i = 0; i <= res; i++) {
    float a = a0 + da * (float)i / (float)res;
    Vec2 p = v2(cx + cosf(a) * r, cy + sinf(a) * r);
    int n = arr_len(*out);
    if (n && fabsf((*out)[n - 1].x - p.x) < 1e-6f && fabsf((*out)[n - 1].y - p.y) < 1e-6f) continue;
    arr_push(*out, p);
  }
  int n = arr_len(*out);
  if (n > 1 && fabsf((*out)[0].x - (*out)[n - 1].x) < 1e-5f && fabsf((*out)[0].y - (*out)[n - 1].y) < 1e-5f)
    arr_pop(*out);
}

static void lay_flat(Mesh *m, float dy) {
  Mat4 t = m4mul(m4translate(v3(0, dy, 0)), m4rot_x(-PI / 2));
  mesh_transform(m, t);
}

Mesh *mesh_holed_slab(float w, float d, float t, const Vec2 *centers, const float *radii, int n) {
  Outline o = {0};
  arr_push(o.outer, v2(-w / 2, -d / 2));
  arr_push(o.outer, v2(w / 2, -d / 2));
  arr_push(o.outer, v2(w / 2, d / 2));
  arr_push(o.outer, v2(-w / 2, d / 2));
  for (int i = 0; i < n; i++) {
    Vec2 *h = NULL;
    arc_points(&h, centers[i].x, -centers[i].y, radii[i], 0, -TAU, 96);
    arr_push(o.holes, h);
  }
  Mesh *m = mesh_extrude(&o, t, 0, 0, 0);
  lay_flat(m, -t / 2);
  outline_free(&o);
  return m;
}

Mesh *mesh_outline(const Vec2 *pts, int npts, float t, const Vec2 *centers, const float *radii, int n) {
  Outline o = {0};
  for (int i = 0; i < npts; i++) arr_push(o.outer, v2(pts[i].x, -pts[i].y));
  for (int i = 0; i < n; i++) {
    Vec2 *h = NULL;
    arc_points(&h, centers[i].x, -centers[i].y, radii[i], 0, -TAU, 48);
    arr_push(o.holes, h);
  }
  Mesh *m = mesh_extrude(&o, t, t * 0.08f, t * 0.08f, 2);
  lay_flat(m, -t / 2);
  outline_free(&o);
  return m;
}

Mesh *mesh_port(const char *kind, float w, float h, float d) {
  Vec2 *outer = NULL;
  arr_push(outer, v2(-d / 2, -h / 2));
  arr_push(outer, v2(d / 2, -h / 2));
  arr_push(outer, v2(d / 2, h / 2));
  arr_push(outer, v2(-d / 2, h / 2));
  float t = minf(h, d) * 0.12f, ow = d / 2 - t, oh = h / 2 - t;
  Vec2 *hole = NULL;
  bool usbc = false;
  if (kind && !strcmp(kind, "hdmi")) {
    float c = oh * 0.55f;
    arr_push(hole, v2(-ow + c, -oh));
    arr_push(hole, v2(ow - c, -oh));
    arr_push(hole, v2(ow, -oh + c));
    arr_push(hole, v2(ow, oh));
    arr_push(hole, v2(-ow, oh));
    arr_push(hole, v2(-ow, -oh + c));
  } else if (kind && !strcmp(kind, "usbc")) {
    usbc = true;
    arc_points(&hole, -ow + oh, 0, oh, PI / 2, PI, 24);
    arc_points(&hole, ow - oh, 0, oh, -PI / 2, PI, 24);
  } else {
    float c = oh * 0.7f;
    arr_push(hole, v2(-ow + c, -oh));
    arr_push(hole, v2(ow, -oh));
    arr_push(hole, v2(ow, oh));
    arr_push(hole, v2(-ow, oh));
    arr_push(hole, v2(-ow, -oh + c));
  }
  Outline shell_o = {0}, back_o = {0};
  memcpy(arr_add(shell_o.outer, 4), outer, sizeof(Vec2) * 4);
  memcpy(arr_add(back_o.outer, 4), outer, sizeof(Vec2) * 4);
  arr_push(shell_o.holes, hole);
  Mesh *shell = mesh_extrude(&shell_o, w * 0.6f, 0, 0, 0);
  Mesh *back = mesh_extrude(&back_o, w * 0.4f, 0, 0, 0);
  mesh_transform(back, m4translate(v3(0, 0, w * 0.6f)));
  Mesh *tongue = mesh_box(ow * 1.4f, oh * 0.45f, w * 0.55f);
  mesh_to_triangles(tongue);
  mesh_transform(tongue, m4translate(v3(0, usbc ? 0 : -oh * 0.1f, w * 0.6f - w * 0.275f)));
  Mesh *m = mesh_new();
  mesh_append(m, shell);
  mesh_append(m, back);
  int n1 = arr_len(m->v);
  mesh_append(m, tongue);
  mesh_transform(m, m4mul(m4translate(v3(-w / 2, 0, 0)), m4rot_y(PI / 2)));
  m->groups[0] = (MeshGroup){0, n1, 0};
  m->groups[1] = (MeshGroup){n1, arr_len(m->v) - n1, 1};
  m->ngroups = 2;
  mesh_flat_normals(m);
  mesh_free(shell);
  mesh_free(back);
  mesh_free(tongue);
  outline_free(&shell_o);
  outline_free(&back_o);
  arr_free(outer);
  return m;
}

Mesh *mesh_fan(float radius, float height, int blades, bool ring, bool prop) {
  float hub = radius * (prop ? 0.11f : 0.3f);
  Mesh *merged = mesh_new();
  Mesh *c = mesh_cylinder(hub, height, 32);
  mesh_to_triangles(c);
  mesh_append(merged, c);
  mesh_free(c);
  float sweep = TAU / (float)blades;
  for (int b = 0; b < blades; b++) {
    Outline o = {0};
    const int steps = 10;
#define EDGE(tv, side, out)                                                                              \
  do {                                                                                                   \
    float t_ = (tv);                                                                                     \
    float r_ = hub * 0.95f + (radius * (ring ? 0.97f : 1.f) - hub * 0.95f) * t_;                         \
    float w_ = prop ? (0.5f + 0.8f * sinf(PI * minf(1, t_ * 1.4f + 0.15f))) * minf(1, (1 - t_) * 6 + 0.25f) * radius * 0.17f / \
                          maxf(r_ * sweep, 1e-6f)                                                        \
                    : 0.45f + 0.4f * t_;                                                                 \
    float a_ = sweep * (0.5f + ((side) - 0.5f) * w_) + t_ * sweep * (prop ? 0.08f : 0.3f);               \
    (out) = v2(cosf(a_) * r_, sinf(a_) * r_);                                                            \
  } while (0)
    Vec2 p;
    for (int k = 0; k <= steps; k++) {
      EDGE((float)k / (float)steps, 0.f, p);
      arr_push(o.outer, p);
    }
    for (int k = steps; k >= 0; k--) {
      EDGE((float)k / (float)steps, 1.f, p);
      arr_push(o.outer, p);
    }
    Mesh *blade = mesh_extrude(&o, maxf(height * 0.1f, 0.2f), 0, 0, 0);
    Mat4 t = m4rot_x(-PI / 2);
    t = m4mul(m4translate(v3(0, -height * 0.05f, 0)), t);
    t = m4mul(m4rot_y(-(float)b * sweep), t);
    float mid = -(float)b * sweep - sweep * 0.65f;
    t = m4mul(m4rot_axis(v3(cosf(mid), 0, -sinf(mid)), 0.22f), t);
    mesh_transform(blade, t);
    mesh_append(merged, blade);
    mesh_free(blade);
    outline_free(&o);
  }
  if (ring) {
    Vec2 pts[5] = {{radius * 0.96f, -height * 0.25f},
                   {radius, -height * 0.25f},
                   {radius, height * 0.25f},
                   {radius * 0.96f, height * 0.25f},
                   {radius * 0.96f, -height * 0.25f}};
    Mesh *band = mesh_lathe(pts, 5, 64);
    mesh_to_triangles(band);
    mesh_update_box(band);
    mesh_append(merged, band);
    mesh_free(band);
  }
  mesh_flat_normals(merged);
  mesh_update_box(merged);
  return merged;
}

// ------------------------------------------------------------------ wires
// Centripetal Catmull-Rom through the points, sampled by arc length.
typedef struct {
  float c0, c1, c2, c3;
} Cubic;
static Cubic cubic_nonuniform(float x0, float x1, float x2, float x3, float dt0, float dt1, float dt2) {
  float t1 = (x1 - x0) / dt0 - (x2 - x0) / (dt0 + dt1) + (x2 - x1) / dt1;
  float t2 = (x2 - x1) / dt1 - (x3 - x1) / (dt1 + dt2) + (x3 - x2) / dt2;
  t1 *= dt1;
  t2 *= dt1;
  return (Cubic){x1, t1, -3 * x1 + 3 * x2 - 2 * t1 - t2, 2 * x1 - 2 * x2 + t1 + t2};
}
static float cubic_at(Cubic c, float t) { return c.c0 + t * (c.c1 + t * (c.c2 + t * c.c3)); }
static float dist2(Vec3 a, Vec3 b) {
  Vec3 d = v3sub(a, b);
  return v3dot(d, d);
}
static Vec3 catmull(const Vec3 *pts, int n, float t) {
  float p = (float)(n - 1) * t;
  int ip = (int)floorf(p);
  float w = p - (float)ip;
  if (w == 0 && ip == n - 1) {
    ip = n - 2;
    w = 1;
  }
  Vec3 p0, p3;
  p0 = ip > 0 ? pts[ip - 1] : v3add(v3sub(pts[0], pts[1]), pts[0]);
  Vec3 p1 = pts[ip], p2 = pts[ip + 1];
  p3 = ip + 2 < n ? pts[ip + 2] : v3add(v3sub(pts[n - 1], pts[n - 2]), pts[n - 1]);
  float d0 = powf(dist2(p0, p1), 0.25f), d1 = powf(dist2(p1, p2), 0.25f), d2 = powf(dist2(p2, p3), 0.25f);
  if (d1 < 1e-4f) d1 = 1;
  if (d0 < 1e-4f) d0 = d1;
  if (d2 < 1e-4f) d2 = d1;
  Cubic x = cubic_nonuniform(p0.x, p1.x, p2.x, p3.x, d0, d1, d2);
  Cubic y = cubic_nonuniform(p0.y, p1.y, p2.y, p3.y, d0, d1, d2);
  Cubic z = cubic_nonuniform(p0.z, p1.z, p2.z, p3.z, d0, d1, d2);
  return v3(cubic_at(x, w), cubic_at(y, w), cubic_at(z, w));
}

#define ARC_DIV 200
static void arc_table(const Vec3 *pts, int n, float *table) {
  Vec3 last = catmull(pts, n, 0);
  float sum = 0;
  table[0] = 0;
  for (int i = 1; i <= ARC_DIV; i++) {
    Vec3 c = catmull(pts, n, (float)i / ARC_DIV);
    sum += v3dist(c, last);
    table[i] = sum;
    last = c;
  }
}
static float arc_u_to_t(const float *table, float u) {
  float target = u * table[ARC_DIV];
  int lo = 0, hi = ARC_DIV;
  while (lo < hi - 1) {
    int mid = (lo + hi) / 2;
    if (table[mid] <= target) lo = mid;
    else hi = mid;
  }
  float seg = table[hi] - table[lo];
  float f = seg > 0 ? (target - table[lo]) / seg : 0;
  return ((float)lo + clampf(f, 0, 1)) / ARC_DIV;
}

float curve_length(const Vec3 *pts, int n) {
  float table[ARC_DIV + 1];
  arc_table(pts, n, table);
  return table[ARC_DIV];
}

void mesh_tube(Mesh *m, const Vec3 *pts_in, int npts_in, int segments, float radius) {
  enum { RADIAL = 8 };
  float table[ARC_DIV + 1];
  // A waypoint on top of a wire's end (it often is: the end was clamped toward it) has no direction.
  Vec3 *pts = NULL;
  for (int i = 0; i < npts_in; i++)
    if (!i || v3dist(pts_in[i], arr_last(pts)) > 1e-3f) arr_push(pts, pts_in[i]);
  int npts = arr_len(pts);
  if (npts < 2) {
    arr_push(pts, v3add(pts_in[0], v3(0, 1e-3f, 0)));
    npts = 2;
  }
  arc_table(pts, npts, table);
  int vcount = (segments + 1) * (RADIAL + 1);
  arr_set_len(m->v, vcount);
  Vec3 normal = {0}, tangent0 = {0};
  for (int i = 0; i <= segments; i++) {
    float u = (float)i / (float)segments;
    float t = arc_u_to_t(table, u), d = 0.0001f;
    Vec3 pos = catmull(pts, npts, t);
    Vec3 tan = v3norm(v3sub(catmull(pts, npts, minf(t + d, 1)), catmull(pts, npts, maxf(t - d, 0))));
    if (i == 0) {
      float mn = 1e30f;
      Vec3 ax = v3(1, 0, 0);
      float tx = fabsf(tan.x), ty = fabsf(tan.y), tz = fabsf(tan.z);
      if (tx <= mn) mn = tx, ax = v3(1, 0, 0);
      if (ty <= mn) mn = ty, ax = v3(0, 1, 0);
      if (tz <= mn) ax = v3(0, 0, 1);
      Vec3 vec = v3norm(v3cross(tan, ax));
      normal = v3cross(tan, vec);
    } else {
      Vec3 vec = v3cross(tangent0, tan);
      float l = v3len(vec);
      if (l > 1e-7f) {
        vec = v3scale(vec, 1 / l);
        float th = acosf(clampf(v3dot(tangent0, tan), -1, 1));
        normal = m4dir(m4rot_axis(vec, th), normal);
      }
    }
    tangent0 = tan;
    Vec3 bin = v3norm(v3cross(tan, normal));
    Vec3 nrm = v3norm(normal);
    for (int j = 0; j <= RADIAL; j++) {
      float v = (float)j / RADIAL * TAU;
      float sn = sinf(v), cs = -cosf(v);
      Vec3 n = v3norm(v3add(v3scale(nrm, cs), v3scale(bin, sn)));
      Vec3 p = v3mad(pos, n, radius);
      m->v[i * (RADIAL + 1) + j] = (Vertex){p.x, p.y, p.z, n.x, n.y, n.z, u, (float)j / RADIAL, u, 0};
    }
  }
  if (!m->idx) {
    for (int j = 1; j <= segments; j++)
      for (int i = 1; i <= RADIAL; i++) {
        u32 a = (u32)((RADIAL + 1) * (j - 1) + (i - 1)), b = (u32)((RADIAL + 1) * j + (i - 1));
        u32 c = (u32)((RADIAL + 1) * j + i), d = (u32)((RADIAL + 1) * (j - 1) + i);
        add_tri_idx(m, a, b, d);
        add_tri_idx(m, b, c, d);
      }
  }
  m->gl_dirty = true;
  mesh_update_box(m);
  arr_free(pts);
}

void route45(float ax, float ay, float bx, float by, float bias, Vec2 out[4]) {
  float dx = bx - ax, dy = by - ay;
  float sx = dx > 0 ? 1.f : dx < 0 ? -1.f : 0, sy = dy > 0 ? 1.f : dy < 0 ? -1.f : 0;
  out[0] = v2(ax, ay);
  if (fabsf(dx) > fabsf(dy)) {
    float diag = fabsf(dy), s1 = (fabsf(dx) - diag) * bias;
    float x1 = ax + sx * s1, x2 = x1 + sx * diag;
    out[1] = v2(x1, ay), out[2] = v2(x2, by);
  } else {
    float diag = fabsf(dx), s1 = (fabsf(dy) - diag) * bias;
    float y1 = ay + sy * s1, y2 = y1 + sy * diag;
    out[1] = v2(ax, y1), out[2] = v2(bx, y2);
  }
  out[3] = v2(bx, by);
}
