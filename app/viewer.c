#include "viewer.h"

#define MAX_TRAIL 16
#define MAX_TAGS 14

typedef struct {
  char path[512];
  char name[160];
} Entry;

typedef struct {
  const char *file, *scale, *title, *text, *caption, *into;
} TourLevel;

typedef struct {
  Arena ar;
  char path[512];
  const char *name, *lead, *subtitle, *finale_title, *finale_text;
  const char **heading; // arr
  const char **acts;    // arr
  TourLevel *levels;    // arr
  int at;
} Tour;

typedef struct {
  char name[128], file[512], note[128];
} ListItem;

typedef enum { MOVE_NONE, MOVE_DIVE, MOVE_CLIMB } MoveKind;

typedef struct {
  int node;
  Vec3 top;
} Tag;

struct Viewer {
  char root[512];
  bool embed, intro;
  Stage stage;
  AssemblySpec *spec;
  Model *model;
  Entry trail[MAX_TRAIL];
  int ntrail;
  Tour *tour;
  ListItem list[64];
  int nlist;
  bool show_list;
  char status[256];
  char title[160];

  float explode;
  bool labels;
  Tag tags[MAX_TAGS];
  int ntags;
  int flow_at;
  ControlValue carry[8];
  char carry_ids[8][64];
  int ncarry;

  int hover_node, hover_copy;
  int sel_node, sel_copy;
  float sel_t;

  // Dive / climb in progress.
  MoveKind move;
  float move_t, move_dur;
  char next_path[512], next_name[160];
  int next_trail;
  float next_from;
  bool fade_on;
  float fade;
  bool busy;

  bool auto_on;
  float auto_t;
  bool landing, landing_leaving, finale;
  float landing_t;
  bool landing_shown;

  char hud_key[2048];
  float hud_t, cap_t;
  float time;
  float scroll_list;
};

static const char *const FLOW_NAMES[] = {"Off", "Slow", "Realtime", "Burst"};
static const float FLOW_SPEEDS[] = {0, 0.3f, 1, 2.5f};

// ------------------------------------------------------------------ small formats
void fmt_length(char *out, size_t n, float mm) {
#define F(v) ((v) >= 10 ? 0 : 1)
  if (mm >= 1000) snprintf(out, n, "%.*f m", F(mm / 1000), (double)(mm / 1000));
  else if (mm >= 10) snprintf(out, n, "%.*f cm", F(mm / 10), (double)(mm / 10));
  else if (mm >= 1) snprintf(out, n, "%.*f mm", F(mm), (double)mm);
  else snprintf(out, n, "%.*f \xC2\xB5m", F(mm * 1000), (double)(mm * 1000));
#undef F
}

double metres_of(const char *s) {
  if (!s) return NAN;
  const char *p = s;
  while (*p && !(*p >= '0' && *p <= '9')) p++;
  if (!*p) return NAN;
  char *end;
  double v = strtod(p, &end);
  while (*end == ' ') end++;
  struct {
    const char *u;
    double k;
  } units[] = {{"km", 1e3}, {"cm", 1e-2}, {"mm", 1e-3}, {"\xC2\xB5m", 1e-6}, {"um", 1e-6}, {"nm", 1e-9}, {"pm", 1e-12}, {"m", 1}};
  for (int i = 0; i < COUNT(units); i++)
    if (!strncmp(end, units[i].u, strlen(units[i].u))) return v * units[i].k;
  return NAN;
}

static void fmt_mm(char *out, size_t n, float v) {
  float a = fabsf(v);
  snprintf(out, n, "%.*f", a >= 10 ? 0 : a >= 1 ? 1 : 2, (double)v);
  if (strchr(out, '.')) {
    size_t l = strlen(out);
    while (l && out[l - 1] == '0') out[--l] = 0;
    if (l && out[l - 1] == '.') out[--l] = 0;
  }
}

static void scale_label(char *out, size_t n, const Model *m) {
  Vec3 s = box_size(m->box);
  float mm = maxf(s.x, maxf(s.y, s.z));
  if (mm >= 100) snprintf(out, n, "%d cm", (int)lroundf(mm / 10));
  else if (mm >= 10) snprintf(out, n, "%d mm", (int)lroundf(mm));
  else if (mm >= 1) snprintf(out, n, "%.1f mm", (double)mm);
  else snprintf(out, n, "%d \xC2\xB5m", (int)lroundf(mm * 1000));
}

// ------------------------------------------------------------------ the things drawn on top
void draw_tag(float x, float y, float lead, const char *text, float alpha) {
  float size = S(11), sp = S(0.66f), pad = S(6), h = ui_line_height(size) + S(4);
  float w = ui_text_width(size, FONT_REGULAR, sp, text) + 2 * pad;
  float left = x - w / 2, top = y - h;
  ui_rect(rect(left, top, w, h), RGBA(4, 10, 6, (int)(184 * alpha)));
  ui_rect(rect(left, top, maxf(1, S(1)), h), c_alpha(C_ACCENT, alpha));
  ui_text_sp(left + pad, top + S(2), size, FONT_REGULAR, c_alpha(C_TEXT, alpha), sp, text);
  if (lead > 0) ui_rect_grad(rect(x, y, maxf(1, S(1)), lead), c_alpha(C_ACCENT, alpha), c_alpha(C_ACCENT, 0));
}

void draw_bracket(const Camera *cam, float aspect, Box b, float w, float h, u32 color, float thick) {
  Vec3 size = box_size(b);
  float big = maxf(size.x, maxf(size.y, size.z)), pad = big * 0.012f;
  Vec3 lo = v3(b.min.x - pad, b.min.y - pad, b.min.z - pad), hi = v3(b.max.x + pad, b.max.y + pad, b.max.z + pad);
  float len[3] = {minf((size.x + 2 * pad) * 0.22f, big * 0.08f), minf((size.y + 2 * pad) * 0.22f, big * 0.08f), minf((size.z + 2 * pad) * 0.22f, big * 0.08f)};
  for (int c = 0; c < 8; c++) {
    Vec3 corner = v3(c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z);
    float sx, sy;
    if (!camera_project(cam, aspect, corner, w, h, &sx, &sy)) continue;
    for (int axis = 0; axis < 3; axis++) {
      Vec3 end = corner;
      if (axis == 0) end.x += (c & 1 ? -1 : 1) * len[0];
      if (axis == 1) end.y += (c & 2 ? -1 : 1) * len[1];
      if (axis == 2) end.z += (c & 4 ? -1 : 1) * len[2];
      float ex, ey;
      if (camera_project(cam, aspect, end, w, h, &ex, &ey)) ui_line(sx, sy, ex, ey, thick, color);
    }
  }
}

static float ease_out_expo(float t) {
  // The curve text rises with: quick, then settling.
  t = clampf(t, 0, 1);
  return 1 - powf(1 - t, 4);
}

static float rise(float t, float delay, float dur) { return ease_out_expo((t - delay) / dur); }

void draw_hud(float x, float bottom, float maxw, const char *scale, const char *title, const char *text, const char *caption, float t, float cap_t, float alpha) {
  float big = S(minf(88, maxf(40, (float)ui_input()->w / S(1) * 0.07f)));
  float title_size = S(16), body = S(13.5f), body_lh = S(20), cap = S(12.5f), cap_lh = S(18);
  float cap_h = caption && *caption ? ui_wrap_height(maxw - S(12), cap, FONT_REGULAR, cap_lh, caption) : 0;
  float text_h = text && *text ? ui_wrap_height(maxw, body, FONT_REGULAR, body_lh, text) : 0;
  float y = bottom - cap_h;
  float cap_a = rise(cap_t, 0.34f, 0.45f) * alpha;
  if (cap_h > 0) {
    float yy = y + (1 - cap_a / maxf(alpha, 1e-3f)) * S(6);
    ui_rect(rect(x, yy, S(2), cap_h), c_alpha(C_ACCENT, cap_a));
    ui_wrap(x + S(12), yy, maxw - S(12), cap, FONT_REGULAR, c_alpha(C_TEXT, cap_a), cap_lh, caption);
  }
  y -= S(14);
  if (text_h > 0) {
    y -= text_h;
    float a = rise(t, 0.26f, 0.9f);
    ui_wrap(x, y + (1 - a) * S(14), maxw, body, FONT_REGULAR, c_alpha(C_DIM, a * alpha), body_lh, text);
    y -= S(8);
  }
  if (title && *title) {
    y -= ui_line_height(title_size);
    float a = rise(t, 0.18f, 0.9f);
    char upper[256];
    int n = 0;
    for (const char *c = title; *c && n < 255; c++) upper[n++] = *c >= 'a' && *c <= 'z' ? (char)(*c - 32) : *c;
    upper[n] = 0;
    ui_text_sp(x, y + (1 - a) * S(14), title_size, FONT_BOLD, c_alpha(RGBA(0xa6, 0xe0, 0x5a, 255), a * alpha), S(1.8f), upper);
    y -= S(8);
  }
  if (scale && *scale) {
    y -= big * 1.12f;
    float a = rise(t, 0.1f, 0.9f), yy = y + (1 - a) * S(14);
    // A soft green glow behind the numbers.
    for (int i = 0; i < 8; i++) {
      float ang = (float)i / 8 * TAU, r = big * 0.05f;
      ui_text(x + cosf(ang) * r, yy + sinf(ang) * r, big, FONT_REGULAR, c_alpha(C_ACCENT, 0.06f * a * alpha), scale);
    }
    ui_text(x, yy, big, FONT_REGULAR, c_alpha(RGBA(0xf1, 0xf7, 0xec, 255), a * alpha), scale);
  }
}

// ------------------------------------------------------------------ opening things
static void tour_free(Tour *t) {
  if (!t) return;
  arr_free(t->heading);
  arr_free(t->acts);
  arr_free(t->levels);
  arena_free(&t->ar);
  free(t);
}

static Tour *tour_parse(const char *path, const char *text) {
  Arena ar = {0};
  Json *j = json_parse(&ar, text, NULL, 0);
  Json *levels = j ? json_get(j, "levels") : NULL;
  if (!levels || levels->type != J_ARR) {
    arena_free(&ar);
    return NULL;
  }
  Tour *t = calloc(1, sizeof *t);
  t->ar = ar;
  snprintf(t->path, sizeof t->path, "%s", path);
  t->name = json_str(j, "name", "Tour");
  t->lead = json_str(j, "lead", NULL);
  t->subtitle = json_str(j, "subtitle", NULL);
  const Json *fin = json_get(j, "finale");
  t->finale_title = json_str(fin, "title", NULL);
  t->finale_text = json_str(fin, "text", NULL);
  const Json *h = json_get(j, "heading");
  if (h && h->type == J_ARR)
    for (const Json *e = h->head; e; e = e->next) arr_push(t->heading, e->str);
  const Json *acts = json_get(j, "acts");
  if (acts && acts->type == J_ARR)
    for (const Json *e = acts->head; e; e = e->next) arr_push(t->acts, json_str(e, "title", ""));
  for (const Json *e = levels->head; e; e = e->next)
    arr_push(t->levels, ((TourLevel){json_str(e, "file", ""), json_str(e, "scale", NULL), json_str(e, "title", NULL),
                                     json_str(e, "text", NULL), json_str(e, "caption", NULL), json_str(e, "into", NULL)}));
  return t;
}

static const char *file_base(const char *path) {
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

static void load_list(Viewer *v) {
  v->nlist = 0;
  char path[640];
  snprintf(path, sizeof path, "%.500s/models/index.json", v->root);
  char *text = file_read(path, NULL);
  if (!text) return;
  Arena ar = {0};
  Json *j = json_parse(&ar, text, NULL, 0);
  if (j && j->type == J_ARR)
    for (const Json *e = j->head; e && v->nlist < COUNT(v->list); e = e->next) {
      if (json_bool(e, "hidden", false)) continue;
      ListItem *it = &v->list[v->nlist++];
      snprintf(it->name, sizeof it->name, "%s", json_str(e, "name", ""));
      snprintf(it->note, sizeof it->note, "%s", json_str(e, "note", ""));
      snprintf(it->file, sizeof it->file, "%.240s/models/%.240s", v->root, json_str(e, "file", ""));
    }
  arena_free(&ar);
  free(text);
}

Viewer *viewer_new(const char *root, bool embed) {
  Viewer *v = calloc(1, sizeof *v);
  snprintf(v->root, sizeof v->root, "%s", root);
  v->embed = embed;
  v->intro = true;
  v->flow_at = 2;
  v->hover_node = v->sel_node = -1;
  v->hover_copy = v->sel_copy = -1;
  v->show_list = !embed;
  stage_init(&v->stage);
  if (!embed) load_list(v);
  return v;
}

void viewer_set_intro(Viewer *v, bool intro) { v->intro = intro; }
void viewer_set_status(Viewer *v, const char *text) { snprintf(v->status, sizeof v->status, "%s", text); }
Model *viewer_model(Viewer *v) { return v->model; }
Stage *viewer_stage(Viewer *v) { return &v->stage; }
const char *viewer_title(const Viewer *v) { return v->title; }
bool viewer_busy(const Viewer *v) { return v->busy; }
int viewer_level(const Viewer *v) { return v->tour ? v->tour->at : v->ntrail - 1; }

static void remember_values(Viewer *v) {
  if (!v->model) return;
  for (int i = 0; i < arr_len(v->model->values) && i < 8; i++) {
    snprintf(v->carry_ids[i], sizeof v->carry_ids[i], "%s", v->model->values[i].id);
    v->carry[i] = (ControlValue){v->carry_ids[i], v->model->values[i].value};
    if (i + 1 > v->ncarry) v->ncarry = i + 1;
  }
}

static void build_tags(Viewer *v);

// Swap the model on stage. `from` is how the camera arrives (see stage_frame).
static bool show_file(Viewer *v, const char *path, const char *name, float from, float aspect, char *err, size_t errlen) {
  AssemblySpec *s = spec_load(path, err, errlen);
  if (!s) return false;
  Model *m = model_build(s, (BuildOpts){.paint = true});
  remember_values(v);
  if (v->model) {
    r_release_model(v->model);
    model_free(v->model);
    spec_free(v->spec);
  }
  v->model = m;
  v->spec = s;
  model_start_values(m, v->carry, v->ncarry);
  m->flow_speed = FLOW_SPEEDS[v->flow_at];
  v->explode = 0;
  v->hover_node = v->sel_node = -1;
  v->sel_copy = v->hover_copy = -1;
  if (arr_len(m->missing)) fprintf(stderr, "%s: wires name no part called %s\n", path, m->missing[0]);
  stage_frame(&v->stage, m, aspect, from);
  snprintf(v->title, sizeof v->title, "%s", name && *name ? name : s->name);
  build_tags(v);
  return true;
}

bool viewer_open(Viewer *v, const char *path, char *err, size_t errlen) {
  size_t len;
  char *text = file_read(path, &len);
  if (!text) {
    snprintf(err, errlen, "can't read %s", path);
    return false;
  }
  Tour *t = tour_parse(path, text);
  free(text);
  tour_free(v->tour);
  v->tour = t;
  v->ntrail = 0;
  v->finale = false;
  v->auto_on = false;
  float aspect = 1400.f / 850;
  Arena ar = {0};
  const char *open_path = path, *name = NULL;
  if (t) {
    open_path = path_join(&ar, path, t->levels[0].file);
    name = t->levels[0].title;
    t->at = 0;
    v->landing = v->intro;
    v->landing_leaving = false;
    v->landing_t = 0;
  } else {
    v->landing = false;
  }
  bool ok = show_file(v, open_path, name, 1, aspect, err, errlen);
  if (ok) {
    Entry *e = &v->trail[v->ntrail++];
    snprintf(e->path, sizeof e->path, "%s", open_path);
    snprintf(e->name, sizeof e->name, "%s", v->title);
    v->stage.flying = false;
  }
  arena_free(&ar);
  return ok;
}

void viewer_free(Viewer *v) {
  if (v->model) {
    r_release_model(v->model);
    model_free(v->model);
    spec_free(v->spec);
  }
  tour_free(v->tour);
  free(v);
}

// ------------------------------------------------------------------ labels
typedef struct {
  int node;
  float size;
} Cand;
static int cmp_cand(const void *a, const void *b) {
  float x = ((const Cand *)a)->size, y = ((const Cand *)b)->size;
  return x < y ? 1 : x > y ? -1 : 0;
}

static void build_tags(Viewer *v) {
  v->ntags = 0;
  if (!v->labels || !v->model) return;
  const Model *m = v->model;
  Cand *c = NULL;
  for (int i = 1; i < arr_len(m->nodes); i++) {
    const Node *n = &m->nodes[i];
    if (!n->name[0] || (n->flags & (NF_SLAB | NF_WIRES | NF_WIRE | NF_SCATTER | NF_DECAL | NF_LAYER))) continue;
    if (n->parent == m->root) continue;
    Box b = node_box_now(m, i);
    if (box_is_empty(b)) continue;
    arr_push(c, ((Cand){i, v3len(box_size(b))}));
  }
  if (c) qsort(c, (size_t)arr_len(c), sizeof(Cand), cmp_cand);
  for (int i = 0; i < arr_len(c) && v->ntags < MAX_TAGS; i++) {
    bool inside = false;
    for (int p = m->nodes[c[i].node].parent; p > 0 && !inside; p = m->nodes[p].parent)
      for (int k = 0; k < v->ntags; k++)
        if (v->tags[k].node == p) inside = true;
    if (inside) continue;
    Box b = node_box_now(m, c[i].node);
    Vec3 ctr = box_center(b);
    v->tags[v->ntags++] = (Tag){c[i].node, v3(ctr.x, b.max.y, ctr.z)};
  }
  arr_free(c);
}

static void draw_tags(Viewer *v, float aspect) {
  if (!v->ntags) return;
  const UiInput *in = ui_input();
  float W = (float)in->w, H = (float)in->h;
  // How many fit: one per 70 px of the model's diagonal on screen, at least three.
  Box tb = node_box_now(v->model, v->model->root);
  float x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
  for (int i = 0; i < 8; i++) {
    float sx, sy;
    Vec3 p = v3(i & 1 ? tb.max.x : tb.min.x, i & 2 ? tb.max.y : tb.min.y, i & 4 ? tb.max.z : tb.min.z);
    if (camera_project(&v->stage.cam, aspect, p, W, H, &sx, &sy)) {
      x0 = minf(x0, sx), x1 = maxf(x1, sx), y0 = minf(y0, sy), y1 = maxf(y1, sy);
    }
  }
  int budget = maxi(3, (int)(hypotf(x1 - x0, y1 - y0) / S(70)));
  struct {
    float x, y, w;
  } placed[MAX_TAGS];
  int np = 0;
  for (int i = 0; i < v->ntags; i++) {
    Tag *t = &v->tags[i];
    Node *n = &v->model->nodes[t->node];
    // Follow the piece as it moves.
    Box b = node_box_now(v->model, t->node);
    Vec3 c = box_center(b);
    t->top = v3(c.x, b.max.y, c.z);
    float x, y;
    if (np >= budget || !camera_project(&v->stage.cam, aspect, t->top, W, H, &x, &y)) continue;
    if (x <= 0 || x >= W || y <= 0 || y >= H) continue;
    float w = ui_text_width(S(11), FONT_REGULAR, S(0.66f), n->name) + S(12);
    float ty = y - S(14);
    bool clear = false;
    for (int k = 0; k < 3; k++) {
      int hit = -1;
      for (int q = 0; q < np && hit < 0; q++)
        if (fabsf(placed[q].x - x) < (placed[q].w + w) / 2 + S(8) && fabsf(placed[q].y - ty) < S(22)) hit = q;
      if (hit < 0) {
        clear = true;
        break;
      }
      ty = placed[hit].y - S(23);
    }
    if (!clear) continue;
    placed[np].x = x, placed[np].y = ty, placed[np].w = w;
    np++;
    draw_tag(x, ty, y - ty, n->name, 1);
  }
}

// ------------------------------------------------------------------ selection
static void focus_on(Viewer *v, int node, int copy, bool glide, float aspect) {
  v->sel_node = node;
  v->sel_copy = copy;
  v->sel_t = 0;
  model_make_room(v->model, node);
  if (node >= 0 && glide) {
    Box b = node_copy_box_now(v->model, node, copy);
    Vec3 pos, target;
    stage_focus_pose(&v->stage, b, aspect, &pos, &target);
    stage_fly(&v->stage, pos, target, 0.7f);
  }
}

void viewer_select(Viewer *v, int node, int copy) { focus_on(v, node, copy, false, 1400.f / 850); }
void viewer_set_explode(Viewer *v, float amount) {
  v->explode = amount;
  model_set_explode(v->model, amount);
}
void viewer_set_labels(Viewer *v, bool on) {
  v->labels = on;
  build_tags(v);
}

// ------------------------------------------------------------------ dive and climb
static void begin_dive(Viewer *v, const char *rel, Box *box, const char *name) {
  if (v->move != MOVE_NONE || v->ntrail >= MAX_TRAIL) return;
  Arena ar = {0};
  const char *full = path_join(&ar, v->trail[v->ntrail - 1].path, rel);
  // A tour's first level resolves from the tour itself.
  if (v->tour && v->ntrail == 1 && file_read(full, NULL) == NULL) full = path_join(&ar, v->tour->path, rel);
  snprintf(v->next_path, sizeof v->next_path, "%s", full);
  snprintf(v->next_name, sizeof v->next_name, "%s", name ? name : "");
  arena_free(&ar);
  v->move = MOVE_DIVE;
  v->move_t = 0;
  v->move_dur = 0.9f;
  v->next_from = 5;
  v->next_trail = v->ntrail;
  focus_on(v, -1, -1, false, 1);
  Vec3 c = box ? box_center(*box) : v->stage.cam.target;
  float r = box ? box_radius(*box) : 1;
  Vec3 dir = v3norm(v3sub(v->stage.cam.pos, c));
  stage_fly(&v->stage, v3mad(c, dir, r * 0.3f), c, 0.9f);
}

static void begin_climb(Viewer *v, int to) {
  if (v->move != MOVE_NONE || to < 0 || to >= v->ntrail) return;
  snprintf(v->next_path, sizeof v->next_path, "%s", v->trail[to].path);
  snprintf(v->next_name, sizeof v->next_name, "%s", v->trail[to].name);
  v->move = MOVE_CLIMB;
  v->move_t = 0;
  v->move_dur = 0.6f;
  v->next_from = 0.25f;
  v->next_trail = to;
  focus_on(v, -1, -1, false, 1);
  Vec3 back = v3add(v->stage.cam.target, v3scale(v3sub(v->stage.cam.pos, v->stage.cam.target), 3));
  stage_fly(&v->stage, back, v->stage.cam.target, 0.6f);
  v->fade_on = true;
}

static void move_tick(Viewer *v, float dt, float aspect) {
  if (v->move == MOVE_NONE) return;
  v->move_t += dt;
  if (v->move == MOVE_DIVE && v->move_t >= 0.45f) v->fade_on = true;
  if (v->move_t < v->move_dur) return;
  char err[256] = "";
  MoveKind kind = v->move;
  v->move = MOVE_NONE;
  if (show_file(v, v->next_path, v->next_name, v->next_from, aspect, err, sizeof err)) {
    if (kind == MOVE_DIVE) {
      Entry *e = &v->trail[v->ntrail++];
      snprintf(e->path, sizeof e->path, "%s", v->next_path);
      snprintf(e->name, sizeof e->name, "%s", v->title);
    } else {
      v->ntrail = v->next_trail + 1;
    }
  } else {
    snprintf(v->status, sizeof v->status, "%s", err);
  }
  v->fade_on = false;
}

// Where the tour is, from where you actually are: a manual dive that lands on a tour level keeps
// the tour in step; one that leaves its path makes the HUD describe that model instead.
static bool sync_tour(Viewer *v) {
  if (!v->tour) return false;
  if (v->ntrail <= 1) {
    v->tour->at = 0;
    return true;
  }
  Arena ar = {0};
  bool found = false;
  for (int i = 0; i < arr_len(v->tour->levels) && !found; i++) {
    const char *p = path_join(&ar, v->tour->path, v->tour->levels[i].file);
    if (!strcmp(p, v->trail[v->ntrail - 1].path)) {
      v->tour->at = i;
      found = true;
    }
  }
  arena_free(&ar);
  return found;
}

static bool can_step_next(const Viewer *v) {
  if (v->tour) return true;
  return v->model && model_first_detail(v->model) >= 0;
}

static bool step(Viewer *v, int dir) {
  if (v->move != MOVE_NONE || !v->model) return false;
  if (!v->tour || !sync_tour(v)) {
    if (dir > 0) {
      int part = model_first_detail(v->model);
      if (part < 0) return false;
      Box b = node_box_now(v->model, part);
      begin_dive(v, v->model->nodes[part].detail, &b, v->model->nodes[part].name);
    } else {
      if (v->ntrail < 2) return false;
      begin_climb(v, v->ntrail - 2);
    }
    return true;
  }
  int i = v->tour->at + dir;
  if (i < 0 || i >= arr_len(v->tour->levels)) return false;
  const TourLevel *L = &v->tour->levels[i];
  if (dir > 0) {
    const char *into = v->tour->levels[v->tour->at].into;
    int part = into ? model_find(v->model, into) : -1;
    Box b = part >= 0 ? node_box_now(v->model, part) : box_empty();
    v->tour->at = i;
    Arena ar = {0};
    const char *rel = path_join(&ar, v->tour->path, L->file);
    // The path is absolute within the data now, so resolve it as is.
    v->move = MOVE_NONE;
    snprintf(v->next_path, sizeof v->next_path, "%s", rel);
    snprintf(v->next_name, sizeof v->next_name, "%s", L->title ? L->title : "");
    arena_free(&ar);
    v->move = MOVE_DIVE;
    v->move_t = 0;
    v->move_dur = 0.9f;
    v->next_from = 5;
    focus_on(v, -1, -1, false, 1);
    Vec3 c = part >= 0 ? box_center(b) : v->stage.cam.target;
    float r = part >= 0 ? box_radius(b) : 1;
    Vec3 d = v3norm(v3sub(v->stage.cam.pos, c));
    stage_fly(&v->stage, v3mad(c, d, r * 0.3f), c, 0.9f);
  } else {
    v->tour->at = i;
    begin_climb(v, v->ntrail - 2);
  }
  return true;
}

void viewer_step(Viewer *v, int dir) { step(v, dir); }

// ------------------------------------------------------------------ panels
static void panel_title(float x, float y, const char *s, u32 c) { ui_text_sp(x, y, S(10.5f), FONT_REGULAR, c, S(1.8f), s); }

static void draw_library(Viewer *v, float aspect) {
  if (v->embed || !v->show_list || !v->nlist) return;
  Rect r = rect(S(32), S(64), S(240), minf(S(370), (float)ui_input()->h - S(200)));
  if (r.h < S(100)) return;
  ui_panel(r);
  panel_title(r.x + S(18), r.y + S(14), "MODELS", C_DIM);
  Rect list = rect(r.x + 1, r.y + S(36), r.w - 2, r.h - S(37));
  Scroll sc = ui_scroll_begin(list, "library");
  float y = sc.y, rowh = S(52);
  for (int i = 0; i < v->nlist; i++) {
    Rect row = rect(list.x + S(10), y, list.w - S(20), rowh);
    bool cur = !strcmp(v->list[i].file, v->tour ? v->tour->path : (v->ntrail ? v->trail[0].path : ""));
    ui_push_id(i);
    bool over = ui_hovering(row);
    if (cur) {
      ui_rect(row, RGBA(20, 36, 10, 255));
      ui_rect(rect(row.x, row.y, S(2), row.h), C_ACCENT);
    } else if (over) ui_rect(row, C_HOVER);
    ui_text_fit(row.x + S(12), row.y + S(7), row.w - S(18), S(13.5f), FONT_REGULAR, C_TEXT, v->list[i].name);
    char note[128];
    int n = 0;
    for (const char *c = v->list[i].note; *c && n < 127; c++) note[n++] = *c >= 'a' && *c <= 'z' ? (char)(*c - 32) : *c;
    note[n] = 0;
    ui_text_sp(row.x + S(12), row.y + S(28), S(10), FONT_REGULAR, C_FAINT, S(1.1f), note);
    if (over && ui_input()->pressed[0] && !ui_wants_keyboard()) {
      char err[256] = "";
      if (!viewer_open(v, v->list[i].file, err, sizeof err)) snprintf(v->status, sizeof v->status, "%s", err);
      else {
        v->status[0] = 0;
        stage_frame(&v->stage, v->model, aspect, 1);
      }
    }
    ui_pop_id();
    y += rowh;
  }
  ui_scroll_end(&sc, y);
}

static void draw_controls(Viewer *v, const UiInput *in) {
  const AssemblySpec *s = v->spec;
  int nc = arr_len(s->controls), nr = arr_len(s->readouts);
  if (!nc && !nr) return;
  float w = S(280), rowr = S(22), pad = S(16);
  float h = pad * 2 + (float)nc * S(48) + (nr ? (float)nr * rowr + S(10) : 0) + S(22);
  Rect r = rect((float)in->w - w - S(32), (float)in->h - h - S(110), w, h);
  ui_panel(r);
  float y = r.y + pad;
  for (int i = 0; i < nc; i++) {
    const ControlSpec *c = &s->controls[i];
    float val = (float)model_get_value(v->model, c->id);
    char txt[64];
    format_readout(txt, sizeof txt, val, c->unit, 0);
    char label[128];
    int n = 0;
    for (const char *t = c->label ? c->label : ""; *t && n < 127; t++) label[n++] = *t >= 'a' && *t <= 'z' ? (char)(*t - 32) : *t;
    label[n] = 0;
    ui_text_sp(r.x + pad, y, S(10.5f), FONT_REGULAR, C_DIM, S(1.5f), label);
    float tw = ui_text_width(S(13), FONT_REGULAR, 0, txt);
    ui_text(r.x + r.w - pad - tw, y - S(1), S(13), FONT_REGULAR, RGBA(0xa6, 0xe0, 0x5a, 255), txt);
    ui_push_id(i);
    float mn = is_set(c->min) ? (float)c->min : 0, mx = is_set(c->max) ? (float)c->max : 100;
    if (ui_slider(rect(r.x + pad - S(8), y + S(18), r.w - 2 * pad + S(16), S(22)), "ctl", &val, mn, mx)) {
      float st = is_set(c->step) ? (float)c->step : 1;
      val = roundf(val / st) * st;
      model_set_value(v->model, c->id, val);
      model_start_values(v->model, v->model->values, arr_len(v->model->values));
    }
    ui_pop_id();
    y += S(48);
  }
  if (nr) {
    ui_rect(rect(r.x + pad, y, r.w - 2 * pad, maxf(1, S(1))), C_LINE);
    y += S(8);
    for (int i = 0; i < nr; i++) {
      const ReadoutSpec *ro = &s->readouts[i];
      char txt[64];
      double val = lookup(ro->map, model_get_value(v->model, ro->by ? ro->by : ""));
      format_readout(txt, sizeof txt, val, ro->unit, is_set(ro->digits) ? (int)ro->digits : 0);
      ui_text(r.x + pad, y, S(12), FONT_REGULAR, C_DIM, ro->label ? ro->label : "");
      float tw = ui_text_width(S(12), FONT_REGULAR, 0, txt);
      ui_text(r.x + r.w - pad - tw, y, S(12), FONT_REGULAR, C_TEXT, txt);
      y += rowr;
    }
  }
  char note[64];
  snprintf(note, sizeof note, "Turns shown %d\xC3\x97 slower than real", (int)SPIN_SLOWDOWN);
  ui_text(r.x + pad, r.y + r.h - pad - S(4), S(10.5f), FONT_REGULAR, C_FAINT, note);
}

static void draw_inspector(Viewer *v, const UiInput *in, float aspect) {
  if (v->sel_node < 0) return;
  v->sel_t += in->dt;
  const Model *m = v->model;
  const Node *n = &m->nodes[v->sel_node];
  bool phone = in->w < S(760);
  float w = phone ? (float)in->w - S(24) : S(300), pad = S(18), bodyw = w - 2 * pad;
  char name[160];
  snprintf(name, sizeof name, "%s", n->name);
  if (v->sel_copy >= 0) {
    if (n->names && v->sel_copy < arr_len(n->names)) snprintf(name, sizeof name, "%s", n->names[v->sel_copy]);
    else snprintf(name, sizeof name, "%s %d", n->name, v->sel_copy + 1);
  }
  const char *info = n->info ? n->info : "";
  // Measure, then lay out.
  KV rows[32];
  int nrows = 0;
  char size_text[96], copies[32];
  Box b = v->sel_copy >= 0 ? node_copy_box_now(m, v->sel_node, v->sel_copy) : node_box_now(m, v->sel_node);
  Vec3 sz = box_size(b);
  bool row = n->inst && arr_len(n->inst) > 1;
  if (row && v->sel_copy < 0) sz = box_size(n->mesh->box);
  char a[24], c[24], e[24];
  fmt_mm(a, sizeof a, sz.x), fmt_mm(c, sizeof c, sz.y), fmt_mm(e, sizeof e, sz.z);
  snprintf(size_text, sizeof size_text, "%s \xC3\x97 %s \xC3\x97 %s mm", a, c, e);
  for (int i = 0; n->specs && i < arr_len(n->specs) && nrows < 28; i++) rows[nrows++] = n->specs[i];
  rows[nrows++] = (KV){row && v->sel_copy < 0 ? "Size (each)" : "Size", size_text};
  char mat[64];
  if (n->material_name) {
    snprintf(mat, sizeof mat, "%s", n->material_name);
    if (mat[0] >= 'a' && mat[0] <= 'z') mat[0] = (char)(mat[0] - 32);
    rows[nrows++] = (KV){"Material", mat};
  }
  if (row) {
    if (v->sel_copy >= 0) {
      snprintf(copies, sizeof copies, "%d of %d", v->sel_copy + 1, arr_len(n->inst));
      rows[nrows++] = (KV){"Copy", copies};
    } else {
      snprintf(copies, sizeof copies, "%d", arr_len(n->inst));
      rows[nrows++] = (KV){"Copies", copies};
    }
  }
  float info_h = *info ? ui_wrap_height(bodyw, S(13), FONT_REGULAR, S(19), info) : 0;
  float name_h = ui_wrap_height(bodyw - S(16), S(17), FONT_BOLD, S(22), name);
  float specs_h = 0;
  for (int i = 0; i < nrows; i++) specs_h += maxf(S(20), ui_wrap_height(bodyw * 0.56f, S(11.5f), FONT_REGULAR, S(16), rows[i].val) + S(4));
  int nkids = 0;
  if (v->sel_copy < 0)
    for (int k = n->first; k >= 0; k = m->nodes[k].next)
      if (m->nodes[k].name[0] && !(m->nodes[k].flags & NF_DECAL)) nkids++;
  float kids_h = nkids ? S(26) + (float)nkids * S(22) : 0;
  float total = pad * 2 + S(18) + name_h + S(8) + info_h + S(12) + specs_h + S(10) + kids_h + S(40);
  float maxh = (float)in->h - S(180);
  float h = minf(total, maxh);
  float slide = (1 - ease_out_expo(v->sel_t / 0.3f)) * S(14);
  Rect r = phone ? rect(S(12), (float)in->h - h - S(84), w, h) : rect((float)in->w - w - S(32) + slide, ((float)in->h - h) / 2, w, h);
  ui_panel(r);
  ui_rect(rect(r.x, r.y, S(2), r.h), C_ACCENT);
  if (ui_button(rect(r.x + r.w - S(30), r.y + S(8), S(22), S(22)), "\xC3\x97##close", BTN_QUIET)) {
    focus_on(v, -1, -1, false, aspect);
    return;
  }
  Scroll sc = ui_scroll_begin(rect(r.x + 1, r.y + 1, r.w - 2, r.h - S(44)), "inspector");
  float x = r.x + pad, y = sc.y + pad - 1;
  const char *cat = n->category ? n->category : "Part";
  char upper[128];
  int un = 0;
  for (const char *t = cat; *t && un < 127; t++) upper[un++] = *t >= 'a' && *t <= 'z' ? (char)(*t - 32) : *t;
  upper[un] = 0;
  ui_text_sp(x, y, S(10), FONT_REGULAR, RGBA(0xa6, 0xe0, 0x5a, 255), S(1.5f), upper);
  y += S(18);
  y += ui_wrap(x, y, bodyw - S(16), S(17), FONT_BOLD, C_TEXT, S(22), name) + S(8);
  if (*info) y += ui_wrap(x, y, bodyw, S(13), FONT_REGULAR, C_DIM, S(19), info) + S(12);
  for (int i = 0; i < nrows; i++) {
    ui_text_fit(x, y, bodyw * 0.42f, S(11.5f), FONT_REGULAR, C_FAINT, rows[i].key);
    float dh = ui_wrap(x + bodyw * 0.44f, y, bodyw * 0.56f, S(11.5f), FONT_REGULAR, C_TEXT, S(16), rows[i].val);
    y += maxf(S(20), dh + S(4));
  }
  y += S(10);
  if (nkids) {
    ui_text_sp(x, y, S(10), FONT_REGULAR, C_DIM, S(1.5f), "CONSISTS OF");
    y += S(22);
    int idx = 0;
    int clicked = -1;
    for (int k = n->first; k >= 0; k = m->nodes[k].next) {
      const Node *kid = &m->nodes[k];
      if (!kid->name[0] || (kid->flags & NF_DECAL)) continue;
      ui_push_id(idx++);
      char lab[180];
      snprintf(lab, sizeof lab, "%s", kid->name);
      if (ui_button(rect(x - S(4), y, bodyw + S(8), S(20)), lab, BTN_QUIET | BTN_LEFT)) clicked = k;
      ui_pop_id();
      y += S(22);
    }
    if (clicked >= 0) {
      ui_scroll_end(&sc, y);
      focus_on(v, clicked, -1, true, aspect);
      return;
    }
  }
  ui_scroll_end(&sc, y);
  float by = r.y + r.h - S(36);
  float bx = r.x + pad;
  if (n->detail && *n->detail) {
    if (ui_button(rect(bx, by, S(110), S(26)), "Dive in \xE2\x96\xB6", 0)) {
      char nm[160];
      snprintf(nm, sizeof nm, "%s", name);
      Box bb = b;
      begin_dive(v, n->detail, &bb, nm);
      return;
    }
    bx += S(118);
  }
  if (ui_button(rect(bx, by, S(110), S(26)), "Re-centre", 0)) focus_on(v, v->sel_node, v->sel_copy, true, aspect);
}

static void draw_tools(Viewer *v, const UiInput *in) {
  bool has_flow = false;
  for (int i = 0; i < arr_len(v->model->nodes) && !has_flow; i++) has_flow = v->model->nodes[i].flags & (NF_FLOW | NF_WIRE);
  bool can_step = v->tour || can_step_next(v) || v->ntrail > 1;
  float bh = S(32), gap = S(8), pad = S(12);
  float w = pad * 2 + S(180) + S(170);
  if (can_step) w += S(40) + S(56) + S(40) + 2 * gap + S(18);
  if (has_flow) w += S(148) + gap;
  w += S(74) + gap;
  bool phone = (float)in->w < w + S(20);
  float x = ((float)in->w - minf(w, (float)in->w - S(16))) / 2;
  Rect r = rect(x, (float)in->h - S(70), minf(w, (float)in->w - S(16)), S(50));
  ui_panel(r);
  float cx = r.x + pad, cy = r.y + (r.h - bh) / 2;
  if (can_step) {
    if (ui_button(rect(cx, cy, S(40), bh), "\xE2\x97\x80##prev", (v->tour ? v->tour->at == 0 : v->ntrail < 2) ? BTN_DISABLED : 0)) step(v, -1);
    cx += S(40) + gap;
    if (ui_button(rect(cx, cy, S(56), bh), "AUTO", v->auto_on ? BTN_ON : 0)) {
      v->auto_on = !v->auto_on;
      v->auto_t = 0;
    }
    cx += S(56) + gap;
    if (ui_button(rect(cx, cy, S(40), bh), "\xE2\x96\xB6##next", !v->tour && model_first_detail(v->model) < 0 ? BTN_DISABLED : 0)) {
      if (!step(v, 1) && v->tour && v->tour->at == arr_len(v->tour->levels) - 1) v->finale = true;
    }
    cx += S(40) + S(18);
  }
  if (has_flow) {
    char lab[48];
    snprintf(lab, sizeof lab, "FLOW \xC2\xB7 %s##flow", FLOW_NAMES[v->flow_at]);
    for (char *t = lab + 7; *t && *t != '#'; t++)
      if (*t >= 'a' && *t <= 'z') *t = (char)(*t - 32);
    if (ui_button(rect(cx, cy, S(148), bh), lab, 0)) {
      v->flow_at = (v->flow_at + 1) % 4;
      v->model->flow_speed = FLOW_SPEEDS[v->flow_at];
    }
    cx += S(148) + gap;
  }
  if (ui_button(rect(cx, cy, S(74), bh), "LABELS", v->labels ? BTN_ON : 0)) viewer_set_labels(v, !v->labels);
  cx += S(74) + S(16);
  if (!phone || cx + S(150) < r.x + r.w) {
    float tw = ui_text_sp(cx, cy + (bh - ui_line_height(S(10.5f))) / 2, S(10.5f), FONT_REGULAR, C_DIM, S(1.5f), "DISASSEMBLE");
    cx += tw + S(6);
    float amount = v->explode;
    if (ui_slider(rect(cx, cy, r.x + r.w - pad - cx, bh), "explode", &amount, 0, 1)) {
      v->explode = amount;
      model_set_explode(v->model, amount);
      if (v->sel_node >= 0) model_make_room(v->model, v->sel_node);
    }
  }
}

static void draw_crumbs(Viewer *v) {
  if (v->ntrail < 2) return;
  float x = S(32), y = S(44), size = S(11);
  for (int i = 0; i < v->ntrail; i++) {
    if (i) x += ui_text(x, y, size, FONT_REGULAR, C_FAINT, " \xE2\x80\xBA ");
    if (i == v->ntrail - 1) {
      ui_text(x, y, size, FONT_REGULAR, C_DIM, v->trail[i].name);
      break;
    }
    float w = ui_text_width(size, FONT_REGULAR, 0, v->trail[i].name);
    ui_push_id(i);
    if (ui_button(rect(x - S(2), y - S(1), w + S(4), S(18)), v->trail[i].name, BTN_QUIET)) begin_climb(v, i);
    ui_pop_id();
    ui_block(rect(x, y, w, S(18)));
    x += w;
  }
}

// What you are looking at, in sizes everyone knows.
static const struct {
  float mm;
  const char *text;
} SIZES[] = {{1700, "a person"}, {180, "a hand"}, {70, "a credit card"}, {10, "a fingernail"}, {1, "a grain of sand"}, {0.08f, "a hair"}, {0.008f, "a red blood cell"}};

static float fov_across(const Viewer *v, float aspect) {
  float d = v3dist(v->stage.cam.pos, v->stage.cam.target);
  float hfov = 2 * atanf(tanf(v->stage.cam.fov_deg * PI / 360) * aspect);
  return 2 * d * tanf(hfov / 2);
}

static void draw_hud_panel(Viewer *v, const UiInput *in, float aspect) {
  bool on_tour = sync_tour(v);
  bool can_step = v->tour || can_step_next(v) || v->ntrail > 1;
  if (!can_step || !v->model) return;
  char scale[32] = "", title[200] = "", text[1024] = "", cap[300] = "";
  char counter[32];
  if (v->tour && on_tour) {
    const TourLevel *L = &v->tour->levels[v->tour->at];
    snprintf(scale, sizeof scale, "%s", L->scale ? L->scale : "");
    snprintf(title, sizeof title, "%s", L->title ? L->title : "");
    snprintf(text, sizeof text, "%s", L->text ? L->text : "");
    snprintf(cap, sizeof cap, "%s", L->caption ? L->caption : "");
    snprintf(counter, sizeof counter, "%02d / %02d", v->tour->at + 1, arr_len(v->tour->levels));
  } else {
    scale_label(scale, sizeof scale, v->model);
    snprintf(title, sizeof title, "%s", v->ntrail ? v->trail[v->ntrail - 1].name : "");
    int next = model_first_detail(v->model);
    if (next >= 0) snprintf(cap, sizeof cap, "Next: %s", v->model->nodes[next].name);
    else snprintf(cap, sizeof cap, "Deepest level");
    snprintf(counter, sizeof counter, "Level %02d", v->ntrail);
  }
  char key[2048];
  snprintf(key, sizeof key, "%s|%s|%s", scale, title, text);
  if (strcmp(key, v->hud_key)) {
    snprintf(v->hud_key, sizeof v->hud_key, "%s", key);
    v->hud_t = 0;
    v->cap_t = 0;
  }
  v->hud_t += in->dt;
  v->cap_t += in->dt;
  bool phone = in->w < S(760);
  // The live scale line under the HUD.
  float across = fov_across(v, aspect);
  float zoom = (float)in->w / S(1) * (25.4f / 96) / across;
  int best = 0;
  for (int i = 1; i < COUNT(SIZES); i++)
    if (fabsf(logf(SIZES[i].mm / across)) < fabsf(logf(SIZES[best].mm / across))) best = i;
  char len[32], line[120];
  fmt_length(len, sizeof len, across);
  if (zoom >= 10) snprintf(line, sizeof line, "\xE2\x89\x88 %s across \xC2\xB7 \xC3\x97%d \xC2\xB7 about %s", len, (int)lroundf(zoom), SIZES[best].text);
  else snprintf(line, sizeof line, "\xE2\x89\x88 %s across \xC2\xB7 \xC3\x97%.1f \xC2\xB7 about %s", len, (double)zoom, SIZES[best].text);
  float bottom = (float)in->h - S(110);
  if (!phone) {
    ui_text_sp(S(32), bottom - S(16), S(12), FONT_REGULAR, C_DIM, S(0.48f), line);
    bottom -= S(28);
  }
  draw_hud(S(32), bottom, minf(S(520), (float)in->w - S(32)), scale, title, phone ? "" : text, cap, v->hud_t, v->cap_t, 1);
  float cw = ui_text_width(S(12), FONT_REGULAR, S(1.8f), counter);
  ui_text_sp((float)in->w - S(32) - cw, S(54), S(12), FONT_REGULAR, C_FAINT, S(1.8f), counter);
}

static void draw_ruler(Viewer *v, float aspect) {
  if (!v->tour) return;
  const UiInput *in = ui_input();
  if (in->w < S(760)) return;
  // A logarithmic ruler: a tick per power of ten, a dot per level, a marker with the live view.
  double top = -1e30, bottom = 1e30;
  int n = arr_len(v->tour->levels);
  for (int i = 0; i < n; i++) {
    double m = metres_of(v->tour->levels[i].scale);
    if (!isfinite(m) || m <= 0) continue;
    double l = log10(m);
    if (l > top) top = l;
    if (l < bottom) bottom = l;
  }
  if (top < -1e29) return;
  top = ceil(top * 2) / 2 + 0.5;
  bottom = floor(bottom) - 1;
  float x = (float)in->w - S(26), y0 = (float)in->h * 0.2f, y1 = (float)in->h * 0.68f;
#define RY(m) (y0 + (float)((top - log10(m)) / (top - bottom)) * (y1 - y0))
  ui_rect(rect(x, y0, maxf(1, S(1)), y1 - y0), C_LINE);
  for (int e = (int)floor(top); e >= (int)ceil(bottom); e--) {
    float y = RY(pow(10, e));
    ui_rect(rect(x - S(6), y, S(6), maxf(1, S(1))), C_FAINT);
    char t[16];
    snprintf(t, sizeof t, "1e%d", e);
    float w = ui_text_width(S(9), FONT_REGULAR, 0, t);
    ui_text(x - S(10) - w, y - S(6), S(9), FONT_REGULAR, C_FAINT, t);
  }
  bool on = sync_tour(v);
  for (int i = 0; i < n; i++) {
    double m = metres_of(v->tour->levels[i].scale);
    if (!isfinite(m) || m <= 0) continue;
    ui_circle(x + S(0.5f), RY(m), S(3), on && i == v->tour->at ? C_BRIGHT : C_ACCENT);
  }
  double metres = fov_across(v, aspect) / 1000;
  float my = clampf(RY(maxf((float)metres, 1e-13f)), y0, y1);
  ui_rect(rect(x - S(10), my, S(16), maxf(1, S(2))), C_BRIGHT);
#undef RY
}

static void card_corners(Rect r) {
  float t = maxf(S(2), 1), l = S(14);
  ui_rect(rect(r.x, r.y, l, t), C_ACCENT);
  ui_rect(rect(r.x, r.y, t, l), C_ACCENT);
  ui_rect(rect(r.x + r.w - l, r.y + r.h - t, l, t), C_ACCENT);
  ui_rect(rect(r.x + r.w - t, r.y + r.h - l, t, l), C_ACCENT);
}

static void close_landing(Viewer *v) {
  if (!v->landing || v->landing_leaving) return;
  v->landing_leaving = true;
  v->landing_t = 0;
}

static void draw_landing(Viewer *v, const UiInput *in) {
  if (!v->landing) return;
  float alpha = 1;
  if (v->landing_leaving) {
    v->landing_t += in->dt;
    alpha = 1 - clampf(v->landing_t / 0.7f, 0, 1);
    if (alpha <= 0) {
      v->landing = false;
      return;
    }
  }
  ui_rect_grad(rect(0, 0, (float)in->w, (float)in->h), RGBA(2, 5, 4, (int)(130 * alpha)), RGBA(2, 5, 4, (int)(90 * alpha)));
  const Tour *t = v->tour;
  float w = minf(S(620), (float)in->w - S(32)), pad = S(40), bw = w - 2 * pad;
  float title_size = S(minf(54, maxf(30, (float)in->w / S(1) * 0.05f)));
  float lh = title_size * 1.04f;
  int lines = arr_len(t->heading) ? arr_len(t->heading) : 1;
  float sub_h = t->subtitle ? ui_wrap_height(minf(bw, S(430)), S(14.5f), FONT_REGULAR, S(23), t->subtitle) : 0;
  float h = pad * 2 + S(18) + S(18) + lh * (float)lines + S(14) + sub_h + S(26) + (arr_len(t->acts) ? S(36) : 0) + S(40) + S(26);
  float x = in->w > S(900) ? minf(S(120), (float)in->w * 0.07f) : ((float)in->w - w) / 2;
  Rect r = rect(x, ((float)in->h - h) / 2, w, h);
  ui_rect_grad(r, RGBA(6, 14, 9, (int)(220 * alpha)), RGBA(3, 7, 5, (int)(190 * alpha)));
  ui_border(r, c_alpha(RGBA(118, 185, 0, 71), alpha), maxf(1, S(1)));
  if (alpha > 0.5f) card_corners(r);
  if (!v->landing_leaving) ui_block(rect(0, 0, (float)in->w, (float)in->h));
  float y = r.y + pad;
  char lead[160];
  if (t->lead) snprintf(lead, sizeof lead, "%s", t->lead);
  else snprintf(lead, sizeof lead, "%d levels", arr_len(t->levels));
  for (char *c = lead; *c; c++)
    if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
  ui_text_sp(r.x + pad, y, S(11), FONT_REGULAR, c_alpha(RGBA(0xa6, 0xe0, 0x5a, 255), alpha), S(3), lead);
  y += S(36);
  for (int i = 0; i < lines; i++) {
    const char *line = arr_len(t->heading) ? t->heading[i] : t->name;
    ui_text(r.x + pad, y, title_size, FONT_BOLD, c_alpha(C_TEXT, alpha), line);
    y += lh;
  }
  y += S(14);
  if (t->subtitle) y += ui_wrap(r.x + pad, y, minf(bw, S(430)), S(14.5f), FONT_REGULAR, c_alpha(C_DIM, alpha), S(23), t->subtitle) + S(26);
  if (arr_len(t->acts)) {
    float ax = r.x + pad;
    static const char *const ROMAN[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    for (int i = 0; i < arr_len(t->acts) && i < 10; i++) {
      ax += ui_text_sp(ax, y, S(11), FONT_BOLD, c_alpha(C_ACCENT, alpha), S(1.3f), ROMAN[i]) + S(6);
      char up[96];
      int n = 0;
      for (const char *c = t->acts[i]; *c && n < 95; c++) up[n++] = *c >= 'a' && *c <= 'z' ? (char)(*c - 32) : *c;
      up[n] = 0;
      ax += ui_text_sp(ax, y, S(11), FONT_REGULAR, c_alpha(C_DIM, alpha), S(1.3f), up) + S(20);
    }
    y += S(36);
  }
  if (!v->landing_leaving) {
    if (ui_button(rect(r.x + pad, y, S(150), S(38)), "START THE TOUR", BTN_ON) || in->keys[KEY_ENTER]) {
      close_landing(v);
      v->auto_on = true;
      v->auto_t = 0;
    }
    if (ui_button(rect(r.x + pad + S(160), y, S(190), S(38)), "EXPLORE ON MY OWN", 0) || in->keys[KEY_ESCAPE]) close_landing(v);
  }
  y += S(46);
  ui_text_sp(r.x + pad, y, S(10), FONT_REGULAR, c_alpha(C_FAINT, alpha), S(0.8f), "enter start \xC2\xB7 esc explore \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 step \xC2\xB7 l labels");
}

static void draw_finale(Viewer *v, const UiInput *in) {
  if (!v->finale) return;
  ui_rect_grad(rect(0, 0, (float)in->w, (float)in->h), RGBA(4, 7, 10, 170), RGBA(4, 7, 10, 220));
  ui_block(rect(0, 0, (float)in->w, (float)in->h));
  float x = minf(S(120), (float)in->w * 0.08f), w = (float)in->w - 2 * x;
  float size = S(minf(96, maxf(40, (float)in->w / S(1) * 0.07f)));
  const char *title = v->tour && v->tour->finale_title ? v->tour->finale_title : "End of the tour";
  float tw = minf(w, size * 9);
  float th = ui_wrap_height(tw, size, FONT_REGULAR, size, title);
  const char *sub = v->tour ? v->tour->finale_text : NULL;
  float sh = sub ? ui_wrap_height(minf(w, S(520)), S(15), FONT_REGULAR, S(24), sub) : 0;
  float y = ((float)in->h - th - sh - S(90)) / 2;
  ui_wrap(x, y, tw, size, FONT_REGULAR, C_TEXT, size, title);
  y += th + S(20);
  if (sub) y += ui_wrap(x, y, minf(w, S(520)), S(15), FONT_REGULAR, C_DIM, S(24), sub) + S(24);
  if (ui_button(rect(x, y, S(170), S(38)), "BACK TO THE TOP", BTN_ON)) {
    v->finale = false;
    v->tour->at = 0;
    begin_climb(v, 0);
  }
  if (ui_button(rect(x + S(180), y, S(170), S(38)), "KEEP EXPLORING", 0)) v->finale = false;
}

// ------------------------------------------------------------------ the frame
void viewer_frame(Viewer *v, const UiInput *in, u32 fbo) {
  float dt = minf(in->dt, 0.1f), aspect = stage_aspect(in);
  v->time += dt;
  extern int stage_view_height;
  stage_view_height = in->h;
  ui_begin(in);
  if (!v->model) {
    PostParams p = post_defaults();
    r_render_empty(&p, fbo);
    ui_text(S(32), S(24), S(13), FONT_REGULAR, v->status[0] ? C_ERROR : C_TEXT, v->status[0] ? v->status : "Nothing open");
    draw_library(v, aspect);
    ui_render(fbo);
    return;
  }
  bool covered = v->landing && !v->landing_leaving;

  // Keys.
  if (!ui_wants_keyboard() && !covered) {
    if (in->keys[KEY_L]) viewer_set_labels(v, !v->labels);
    if (in->keys[KEY_ESCAPE]) {
      if (v->finale) v->finale = false;
      else if (v->sel_node >= 0) focus_on(v, -1, -1, false, aspect);
    }
    if (in->keys[KEY_RIGHT] && !step(v, 1) && v->tour && v->tour->at == arr_len(v->tour->levels) - 1) v->finale = true;
    if (in->keys[KEY_LEFT]) step(v, -1);
  }
  if (v->auto_on && !v->landing && v->move == MOVE_NONE) {
    v->auto_t += dt;
    if (v->auto_t >= 7) {
      v->auto_t = 0;
      if (!step(v, 1)) {
        v->auto_on = false;
        if (v->tour) v->finale = true;
      }
    }
  }

  // The mouse in the view.
  Rect view = rect(0, 0, (float)in->w, (float)in->h);
  if (!covered && !v->finale) stage_input(&v->stage, in, view);
  bool over_ui = ui_wants_mouse();
  if (v->move == MOVE_NONE && !covered && !v->finale) {
    if (!over_ui && !v->stage.dragging) {
      Vec3 o, d;
      camera_ray(&v->stage.cam, aspect, in->mx, in->my, (float)in->w, (float)in->h, &o, &d);
      Hit h = model_pick(v->model, o, d);
      v->hover_node = h.node;
      v->hover_copy = h.copy;
    } else if (over_ui) {
      v->hover_node = v->hover_copy = -1;
    }
    if (in->released[0] && v->stage.moved < S(6) && !over_ui) {
      Vec3 o, d;
      camera_ray(&v->stage.cam, aspect, in->mx, in->my, (float)in->w, (float)in->h, &o, &d);
      Hit h = model_pick(v->model, o, d);
      focus_on(v, h.node, h.copy, h.node >= 0, aspect);
    }
  } else {
    v->hover_node = v->hover_copy = -1;
  }

  // Everything that moves on its own.
  bool busy = stage_tick(&v->stage, dt, aspect);
  move_tick(v, dt, aspect);
  busy = model_room_tick(v->model, dt) || busy;
  model_tick(v->model, dt);
  model_update(v->model);
  float fade_to = v->fade_on ? 1.f : 0.f;
  float fade_k = minf(dt / 0.35f, 1);
  v->fade += clampf(fade_to - v->fade, -fade_k, fade_k);
  if (v->move != MOVE_NONE || v->fade != fade_to) busy = true;
  v->busy = true; // flows, spins and grain always have somewhere to go

  // 3D.
  PostParams post = post_defaults();
  post.fade = v->fade;
  post.time = v->time;
  SceneHints hints = {v->hover_node, v->hover_copy, false};
  r_render(v->model, &v->stage.cam, &post, &hints, fbo);

  // Interface.
  float W = (float)in->w, H = (float)in->h;
  if (v->hover_node >= 0 && !(v->hover_node == v->sel_node && v->hover_copy == v->sel_copy)) {
    Box hb = node_copy_box_now(v->model, v->hover_node, v->hover_copy);
    draw_bracket(&v->stage.cam, aspect, hb, W, H, RGBA(255, 255, 255, 89), maxf(1, S(1)));
  }
  if (v->sel_node >= 0) {
    Box sb = node_copy_box_now(v->model, v->sel_node, v->sel_copy);
    draw_bracket(&v->stage.cam, aspect, sb, W, H, c_alpha(C_BRIGHT, 0.95f), maxf(1, S(1.2f)));
  }
  if (v->labels && !covered) draw_tags(v, aspect);
  if (!covered && !v->finale) {
    draw_hud_panel(v, in, aspect);
    draw_ruler(v, aspect);
    char upper[160];
    int n = 0;
    for (const char *c = v->title; *c && n < 159; c++) upper[n++] = *c >= 'a' && *c <= 'z' ? (char)(*c - 32) : *c;
    upper[n] = 0;
    ui_text_sp(S(32), S(24), S(12), FONT_REGULAR, C_TEXT, S(2.2f), upper);
    draw_crumbs(v);
    if (!v->embed) {
      if (ui_button(rect(W - S(32) - S(118), S(20), S(118), S(30)), "ALL MODELS", v->show_list ? BTN_ON : 0)) v->show_list = !v->show_list;
      ui_block(rect(W - S(32) - S(118), S(20), S(118), S(30)));
    }
    draw_library(v, aspect);
    draw_controls(v, in);
    draw_tools(v, in);
    if (v->model) draw_inspector(v, in, aspect);
    // The tip by the pointer.
    if (v->hover_node >= 0 && !(v->hover_node == v->sel_node && v->hover_copy == v->sel_copy) && !v->stage.dragging) {
      const Node *n2 = &v->model->nodes[v->hover_node];
      char name[160];
      if (v->hover_copy >= 0 && n2->names && v->hover_copy < arr_len(n2->names)) snprintf(name, sizeof name, "%s", n2->names[v->hover_copy]);
      else if (v->hover_copy >= 0) snprintf(name, sizeof name, "%s %d", n2->name, v->hover_copy + 1);
      else snprintf(name, sizeof name, "%s", n2->name);
      float tw = minf(S(300), maxf(ui_text_width(S(12.5f), FONT_BOLD, 0, name) + S(20), n2->info && *n2->info ? S(300) : 0));
      float ih = n2->info && *n2->info ? ui_wrap_height(tw - S(20), S(11.5f), FONT_REGULAR, S(16), n2->info) : 0;
      float th = S(30) + ih;
      float tx = minf(in->mx + S(16), W - tw - S(16)), ty = minf(in->my + S(16), H - th - S(16));
      ui_set_layer(1);
      ui_rect(rect(tx, ty, tw, th), RGBA(4, 10, 6, 230));
      ui_border(rect(tx, ty, tw, th), C_LINE, maxf(1, S(1)));
      ui_text(tx + S(10), ty + S(7), S(12.5f), FONT_BOLD, C_TEXT, name);
      if (ih > 0) ui_wrap(tx + S(10), ty + S(26), tw - S(20), S(11.5f), FONT_REGULAR, C_DIM, S(16), n2->info);
      ui_set_layer(0);
    }
    if (v->status[0]) ui_text(S(32), H - S(24), S(12), FONT_REGULAR, C_ERROR, v->status);
  }
  draw_landing(v, in);
  draw_finale(v, in);
  ui_render(fbo);
  (void)busy;
}
