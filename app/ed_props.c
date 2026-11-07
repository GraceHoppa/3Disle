// The right-hand panel of Build: every field of whatever is selected, as a form.
#include "editor_int.h"

typedef struct {
  Editor *e;
  AssemblySpec *s;
  float x, y, w, lw, rh;
} Form;

// ------------------------------------------------------------------ rows
static void changed(Form *f) { ed_doc_changed(f->e); }

static void section(Form *f, const char *title) {
  f->y += S(10);
  ui_text_sp(f->x, f->y, S(10), FONT_BOLD, C_ACCENT, S(1.6f), title);
  ui_rect(rect(f->x, f->y + S(15), f->w, maxf(1, S(1))), C_LINE);
  f->y += S(21);
}

static Rect row(Form *f, const char *label) {
  float lh = ui_line_height(S(11));
  ui_text_fit(f->x, f->y + (f->rh - lh) / 2, f->lw - S(6), S(11), FONT_REGULAR, C_DIM, label);
  Rect r = rect(f->x + f->lw, f->y, f->w - f->lw, f->rh);
  f->y += f->rh + S(3);
  return r;
}

static void note(Form *f, u32 c, const char *text) {
  float h = ui_wrap_height(f->w, S(10.5f), FONT_REGULAR, S(14), text);
  ui_wrap(f->x, f->y, f->w, S(10.5f), FONT_REGULAR, c, S(14), text);
  f->y += h + S(4);
}

// A field whose text can't go straight into the document as it is typed: half a rule or a name that
// is still taken. While it has focus it shows what is being typed; otherwise what the document says.
static bool typed_field(Form *f, Rect r, const char *id, const char *current, char *out, int cap) {
  Editor *e = f->e;
  u32 uid = ui_id(id);
  bool had = ui_focus_is(uid);
  char *buf = had ? e->fbuf : out;
  if (!had) snprintf(out, (size_t)cap, "%s", current ? current : "");
  bool ch = ui_text_field(r, id, buf, had ? (int)sizeof e->fbuf : cap, NULL);
  if (!had && ui_focus_is(uid)) snprintf(e->fbuf, sizeof e->fbuf, "%.*s", cap - 1, out);
  if (had) snprintf(out, (size_t)cap, "%.*s", cap - 1, e->fbuf);
  return ch;
}

static void p_text(Form *f, const char *label, const char **field) {
  Rect r = row(f, label);
  char buf[1024];
  snprintf(buf, sizeof buf, "%s", *field ? *field : "");
  if (ui_text_field(r, label, buf, sizeof buf, NULL)) {
    *field = buf[0] ? spec_str(f->s, buf) : NULL;
    changed(f);
  }
}

static void p_long(Form *f, const char *label, const char **field) {
  ui_text_fit(f->x, f->y, f->w, S(11), FONT_REGULAR, C_DIM, label);
  f->y += S(16);
  Rect r = rect(f->x, f->y, f->w, S(70));
  char buf[4000];
  snprintf(buf, sizeof buf, "%s", *field ? *field : "");
  if (ui_multiline(r, label, buf, sizeof buf)) {
    *field = buf[0] ? spec_str(f->s, buf) : NULL;
    changed(f);
  }
  f->y += S(74);
}

// A number the file may leave out: "auto" until it is set, and × takes it out again.
static void num_rect(Form *f, Rect r, const char *id, double *v, double step, const char *unit, double fallback, bool optional) {
  if (!is_set(*v)) {
    char lbl[96];
    snprintf(lbl, sizeof lbl, "auto##%s", id);
    if (ui_button(r, lbl, BTN_LEFT)) {
      *v = fallback;
      changed(f);
    }
    return;
  }
  float cw = optional ? S(18) : 0;
  if (ui_drag(rect(r.x, r.y, r.w - cw, r.h), id, v, step, unit)) changed(f);
  if (optional) {
    char lbl[96];
    snprintf(lbl, sizeof lbl, "\xC3\x97##%s", id);
    if (ui_button(rect(r.x + r.w - cw, r.y, cw, r.h), lbl, BTN_QUIET)) {
      *v = UNSET;
      changed(f);
    }
  }
}

static void p_num(Form *f, const char *label, double *v, double step, const char *unit, double fallback) { num_rect(f, row(f, label), label, v, step, unit, fallback, true); }
static void p_req(Form *f, const char *label, double *v, double step, const char *unit) { num_rect(f, row(f, label), label, v, step, unit, 0, false); }

static void p_nums(Form *f, const char *label, double **v, const char *const *ids, int n, double step, const char *unit) {
  Rect r = row(f, label);
  float gap = S(3), w = (r.w - gap * (float)(n - 1)) / (float)n;
  char id[96];
  for (int i = 0; i < n; i++) {
    snprintf(id, sizeof id, "%s %s", label, ids[i]);
    if (ui_drag(rect(r.x + (float)i * (w + gap), r.y, w, r.h), id, v[i], step, i == n - 1 ? unit : NULL)) changed(f);
  }
}

static void p_d2(Form *f, const char *label, Dbl2 *v, const char *a, const char *b, double step, const char *unit) {
  double *ptr[2] = {&v->x, &v->y};
  const char *ids[2] = {a, b};
  p_nums(f, label, ptr, ids, 2, step, unit);
}

static void p_tri(Form *f, const char *label, bool *has, bool *v) {
  static const char *const O[] = {"Default", "Yes", "No"};
  int sel = !*has ? 0 : *v ? 1 : 2;
  if (ui_dropdown(row(f, label), label, &sel, O, 3)) {
    *has = sel != 0;
    *v = sel == 1;
    changed(f);
  }
}

// A pick from a list that is really a string in the file. An unknown string shows itself as a last
// option, so opening the file changes nothing the owner wrote.
static void p_pick(Form *f, const char *label, const char **field, const char *const *opts, int n, const char *none) {
  const char *list[48];
  int cnt = 0, sel = -1;
  if (none) list[cnt++] = none;
  for (int i = 0; i < n && cnt < 46; i++) {
    if (*field && !strcmp(*field, opts[i])) sel = cnt;
    list[cnt++] = opts[i];
  }
  if (!*field && none) sel = 0;
  if (*field && sel < 0) {
    sel = cnt;
    list[cnt++] = *field;
  }
  int was = sel;
  if (ui_dropdown(row(f, label), label, &sel, list, cnt) && sel != was) {
    *field = none && sel == 0 ? NULL : spec_str(f->s, list[sel]);
    changed(f);
  }
}

static void p_color(Form *f, const char *label, const char **field) {
  static const char *const SW[] = {"#0d3a1c", "#14181c", "#0c0d0e", "#b08a5a", "#c3c8cf", "#d07a45", "#ffc35a", "#a3201a", "#2f6fd0", "#e8eee6"};
  Rect r = row(f, label);
  float sw = S(18);
  Vec3 c = *field ? hex_srgb(*field) : v3(0, 0, 0);
  char buf[64];
  snprintf(buf, sizeof buf, "%s", *field ? *field : "");
  if (ui_text_field(rect(r.x, r.y, r.w - sw - S(3), r.h), label, buf, sizeof buf, "from material")) {
    *field = buf[0] ? spec_str(f->s, buf) : NULL;
    changed(f);
  }
  Rect well = rect(r.x + r.w - sw, r.y, sw, r.h);
  if (*field) ui_rect(well, RGBA((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), 255));
  ui_border(well, C_LINE, maxf(1, S(1)));
  // Quick colours under the field, only when the field is being worked on.
  float px = f->x + f->lw, pw = (f->w - f->lw) / COUNT(SW);
  for (int i = 0; i < COUNT(SW); i++) {
    Rect b = rect(px + (float)i * pw, f->y - S(1), pw - S(1), S(8));
    Vec3 q = hex_srgb(SW[i]);
    ui_rect(b, RGBA((int)(q.x * 255), (int)(q.y * 255), (int)(q.z * 255), 255));
    char id[96];
    snprintf(id, sizeof id, "%s swatch %d", label, i);
    u32 uid = ui_id(id);
    ui_mark(id, b);
    if (ui_hovering(b) && ui_input()->pressed[0]) {
      (void)uid;
      *field = spec_str(f->s, SW[i]);
      changed(f);
    }
  }
  f->y += S(10);
}

static void p_material(Form *f, const char *label, const char **field) {
  const char *names[32];
  int n = 0;
  for (int i = 0; i < MATERIAL_PRESET_COUNT && n < 32; i++) names[n++] = MATERIAL_PRESETS[i].name;
  p_pick(f, label, field, names, n, "From layer");
}

static void p_glow(Form *f, const char *label, const char **field) {
  static const char *const O[] = {"None", "Accent green", "Colour"};
  int sel = !*field ? 0 : !**field ? 1 : 2;
  if (ui_dropdown(row(f, label), label, &sel, O, 3)) {
    *field = sel == 0 ? NULL : sel == 1 ? spec_str(f->s, "") : spec_str(f->s, "#9cff3a");
    changed(f);
  }
  if (sel == 2) {
    char lbl[96];
    snprintf(lbl, sizeof lbl, "%s colour", label);
    p_color(f, lbl, field);
  }
}

// ------------------------------------------------------------------ rules
static void map_format(const Dbl2 *m, char *out, size_t n) {
  size_t at = 0;
  out[0] = 0;
  for (int i = 0; i < arr_len(m) && at + 2 < n; i++) at += (size_t)snprintf(out + at, n - at, "%s%.6g:%.6g", i ? ", " : "", m[i].x, m[i].y);
}

// "0:0, 100:29000"; false until it is whole.
static bool map_parse(const char *s, Dbl2 **out) {
  Dbl2 *m = NULL;
  const char *p = s;
  while (*p) {
    char *end;
    double x = strtod(p, &end);
    if (end == p || *end != ':') goto bad;
    p = end + 1;
    double y = strtod(p, &end);
    if (end == p) goto bad;
    p = end;
    arr_push(m, ((Dbl2){x, y}));
    while (*p == ' ' || *p == ',') p++;
  }
  if (!m) return false;
  arr_free(*out);
  *out = m;
  return true;
bad:
  arr_free(m);
  return false;
}

static int control_list(const AssemblySpec *s, const char **names, int cap) {
  int n = 0;
  for (int i = 0; i < arr_len(s->controls) && n < cap; i++)
    if (s->controls[i].id) names[n++] = s->controls[i].id;
  return n;
}

static void p_map(Form *f, const char *label, Dbl2 **map) {
  Rect r = row(f, label);
  char cur[512], out[512];
  map_format(*map, cur, sizeof cur);
  if (typed_field(f, r, label, cur, out, sizeof out) && map_parse(out, map)) changed(f);
}

static void p_value(Form *f, const char *label, Value *v, double step, const char *unit, double fallback) {
  static const char *const O[] = {"None", "A number", "Follows a control"};
  int mode = !v->set ? 0 : v->is_rule ? 2 : 1;
  int was = mode;
  if (ui_dropdown(row(f, label), label, &mode, O, 3) && mode != was) {
    v->set = mode != 0;
    v->is_rule = mode == 2;
    if (mode == 1) v->num = is_set(v->num) && v->num != 0 ? v->num : fallback;
    if (mode == 2 && !arr_len(v->map)) {
      arr_push(v->map, ((Dbl2){0, 0}));
      arr_push(v->map, ((Dbl2){100, fallback}));
    }
    if (mode == 2 && !v->by && arr_len(f->s->controls)) v->by = f->s->controls[0].id;
    changed(f);
  }
  char id[96];
  if (mode == 1) {
    snprintf(id, sizeof id, "%s value", label);
    p_req(f, id, &v->num, step, unit);
  } else if (mode == 2) {
    const char *names[24] = {0};
    int n = control_list(f->s, names, 24);
    snprintf(id, sizeof id, "%s by", label);
    p_pick(f, id, &v->by, names, n, NULL);
    snprintf(id, sizeof id, "%s map", label);
    p_map(f, id, &v->map);
    if (!n) note(f, C_ERROR, "No controls in this device yet: add one for this rule to follow.");
  }
}

// ------------------------------------------------------------------ lists
static void p_specs(Form *f, KV **kv, bool *has) {
  section(f, "SPECS");
  int del = -1;
  for (int i = 0; i < arr_len(*kv); i++) {
    ui_push_id(i);
    float gap = S(3), cw = S(18), w = (f->w - cw - 2 * gap) / 2;
    char a[300], b[1024];
    snprintf(a, sizeof a, "%s", (*kv)[i].key);
    snprintf(b, sizeof b, "%s", (*kv)[i].val);
    if (ui_text_field(rect(f->x, f->y, w, f->rh), "spec key", a, sizeof a, "name")) {
      (*kv)[i].key = spec_str(f->s, a);
      changed(f);
    }
    if (ui_text_field(rect(f->x + w + gap, f->y, w, f->rh), "spec val", b, sizeof b, "value")) {
      (*kv)[i].val = spec_str(f->s, b);
      changed(f);
    }
    if (ui_button(rect(f->x + f->w - cw, f->y, cw, f->rh), "\xC3\x97##spec", BTN_QUIET)) del = i;
    ui_pop_id();
    f->y += f->rh + S(3);
  }
  if (del >= 0) {
    arr_remove(*kv, del);
    if (!arr_len(*kv)) *has = false;
    changed(f);
  }
  if (ui_button(rect(f->x, f->y, f->w, f->rh), "+ SPEC", 0)) {
    arr_push(*kv, ((KV){spec_str(f->s, "Spec"), spec_str(f->s, "")}));
    *has = true;
    changed(f);
  }
  f->y += f->rh + S(3);
}

static void p_holes(Form *f, const char *title, Hole **h, bool *has) {
  section(f, title);
  int del = -1;
  for (int i = 0; i < arr_len(*h); i++) {
    ui_push_id(i);
    float gap = S(3), cw = S(18), w = (f->w - cw - 3 * gap) / 3;
    if (ui_drag(rect(f->x, f->y, w, f->rh), "hole x", &(*h)[i].at.x, 0.1, "x")) changed(f);
    if (ui_drag(rect(f->x + w + gap, f->y, w, f->rh), "hole z", &(*h)[i].at.y, 0.1, "z")) changed(f);
    if (ui_drag(rect(f->x + 2 * (w + gap), f->y, w, f->rh), "hole r", &(*h)[i].radius, 0.05, "r")) changed(f);
    if (ui_button(rect(f->x + f->w - cw, f->y, cw, f->rh), "\xC3\x97##hole", BTN_QUIET)) del = i;
    ui_pop_id();
    f->y += f->rh + S(3);
  }
  if (del >= 0) {
    arr_remove(*h, del);
    if (!arr_len(*h)) *has = false;
    changed(f);
  }
  if (ui_button(rect(f->x, f->y, f->w, f->rh), "+ HOLE", 0)) {
    arr_push(*h, ((Hole){{0, 0}, 1}));
    *has = true;
    changed(f);
  }
  f->y += f->rh + S(3);
}

static void p_points(Form *f, Dbl2 **pts) {
  section(f, "OUTLINE POINTS");
  int del = -1;
  for (int i = 0; i < arr_len(*pts); i++) {
    ui_push_id(i);
    float gap = S(3), cw = S(18), w = (f->w - cw - 2 * gap) / 2;
    if (ui_drag(rect(f->x, f->y, w, f->rh), "point x", &(*pts)[i].x, 0.1, "x")) changed(f);
    if (ui_drag(rect(f->x + w + gap, f->y, w, f->rh), "point z", &(*pts)[i].y, 0.1, "z")) changed(f);
    if (ui_button(rect(f->x + f->w - cw, f->y, cw, f->rh), "\xC3\x97##pt", arr_len(*pts) > 3 ? BTN_QUIET : BTN_QUIET | BTN_DISABLED)) del = i;
    ui_pop_id();
    f->y += f->rh + S(3);
  }
  if (del >= 0) {
    arr_remove(*pts, del);
    changed(f);
  }
  if (ui_button(rect(f->x, f->y, f->w, f->rh), "+ POINT", 0)) {
    int n = arr_len(*pts);
    Dbl2 a = n ? (*pts)[n - 1] : (Dbl2){0, 0}, b = n ? (*pts)[0] : (Dbl2){1, 0};
    arr_push(*pts, ((Dbl2){(a.x + b.x) / 2, (a.y + b.y) / 2}));
    changed(f);
  }
  f->y += f->rh + S(3);
}

static void p_via(Form *f, Dbl3 **via, bool *has) {
  section(f, "WAYPOINTS");
  int del = -1;
  for (int i = 0; i < arr_len(*via); i++) {
    ui_push_id(i);
    float gap = S(3), cw = S(18), w = (f->w - cw - 3 * gap) / 3;
    if (ui_drag(rect(f->x, f->y, w, f->rh), "via x", &(*via)[i].x, 0.1, "x")) changed(f);
    if (ui_drag(rect(f->x + w + gap, f->y, w, f->rh), "via y", &(*via)[i].y, 0.1, "y")) changed(f);
    if (ui_drag(rect(f->x + 2 * (w + gap), f->y, w, f->rh), "via z", &(*via)[i].z, 0.1, "z")) changed(f);
    if (ui_button(rect(f->x + f->w - cw, f->y, cw, f->rh), "\xC3\x97##via", BTN_QUIET)) del = i;
    ui_pop_id();
    f->y += f->rh + S(3);
  }
  if (del >= 0) {
    arr_remove(*via, del);
    if (!arr_len(*via)) *has = false;
    changed(f);
  }
  if (ui_button(rect(f->x, f->y, f->w, f->rh), "+ WAYPOINT", 0)) {
    int n = arr_len(*via);
    arr_push(*via, (n ? (*via)[n - 1] : (Dbl3){0, 10, 0}));
    *has = true;
    changed(f);
  }
  f->y += f->rh + S(3);
}

static void p_names(Form *f, const char ***names) {
  int del = -1;
  for (int i = 0; i < arr_len(*names); i++) {
    ui_push_id(i);
    float cw = S(18);
    char buf[300];
    snprintf(buf, sizeof buf, "%s", (*names)[i]);
    if (ui_text_field(rect(f->x, f->y, f->w - cw - S(3), f->rh), "copy name", buf, sizeof buf, NULL)) {
      (*names)[i] = spec_str(f->s, buf);
      changed(f);
    }
    if (ui_button(rect(f->x + f->w - cw, f->y, cw, f->rh), "\xC3\x97##name", BTN_QUIET)) del = i;
    ui_pop_id();
    f->y += f->rh + S(3);
  }
  if (del >= 0) {
    arr_remove(*names, del);
    changed(f);
  }
  if (ui_button(rect(f->x, f->y, f->w, f->rh), "+ COPY NAME", 0)) {
    char buf[96];
    snprintf(buf, sizeof buf, "Copy %d", arr_len(*names) + 1);
    arr_push(*names, spec_str(f->s, buf));
    changed(f);
  }
  f->y += f->rh + S(3);
}

// ------------------------------------------------------------------ names, uniquely
static void p_name(Form *f, const char *label, const char **field) {
  Rect r = row(f, label);
  char out[300];
  bool ch = typed_field(f, r, label, *field, out, sizeof out);
  bool taken = strcmp(out, *field ? *field : "") && spec_name_taken(f->s, out);
  if (taken) {
    ui_border(r, C_ERROR, maxf(1, S(1)));
    note(f, C_ERROR, "Another part or layer already has this name; wires find parts by name.");
  }
  if (ch && !taken && out[0]) {
    char from[300];
    snprintf(from, sizeof from, "%s", *field ? *field : "");
    *field = spec_str(f->s, out);
    ed_rename_everywhere(f->e, from, out);
    changed(f);
  }
}

// ------------------------------------------------------------------ the forms
static void form_identity(Form *f, const char **category, const char **info, const char **detail) {
  p_text(f, "Category", category);
  p_long(f, "What it is (the inspector shows this)", info);
  if (detail) {
    float yy = f->y;
    p_text(f, "Dive into", detail);
    if (*detail && **detail) {
      Editor *e = f->e;
      Doc *d = ed_doc(e);
      if (ui_button(rect(f->x + f->lw, f->y, f->w - f->lw, f->rh), "OPEN THAT FILE", 0)) {
        Arena ar = {0};
        char err[300] = "";
        const char *full = path_join(&ar, d->path[0] ? d->path : "./x", *detail);
        if (ed_open_file(e, full, err, sizeof err) < 0) ed_status(e, true, "%s", err);
        arena_free(&ar);
      }
      f->y += f->rh + S(3);
    }
    (void)yy;
  }
}

static void form_device(Form *f) {
  AssemblySpec *s = f->s;
  section(f, "DEVICE");
  Rect r = row(f, "Name");
  char buf[300];
  snprintf(buf, sizeof buf, "%s", s->name);
  if (ui_text_field(r, "Name", buf, sizeof buf, NULL)) {
    s->name = spec_str(s, buf);
    changed(f);
  }
  p_d2(f, "Footprint", &s->footprint, "width", "depth", 1, "mm");
  section(f, "WHAT IS IN IT");
  int parts = 0, links = 0;
  for (int i = 0; i < arr_len(s->layers); i++) parts += arr_len(s->layers[i].parts), links += arr_len(s->layers[i].links);
  char t[200];
  snprintf(t, sizeof t, "%d layers, %d parts, %d links, %d wires, %d controls, %d readouts.", arr_len(s->layers), parts, links, arr_len(s->wires), arr_len(s->controls), arr_len(s->readouts));
  note(f, C_TEXT, t);
  Doc *d = ed_doc(f->e);
  if (d->model) {
    Box b = d->model->box;
    Vec3 sz = box_size(b);
    snprintf(t, sizeof t, "Built size %.4g \xC3\x97 %.4g \xC3\x97 %.4g mm.", (double)sz.x, (double)sz.y, (double)sz.z);
    note(f, C_DIM, t);
  }
}

static void form_layer(Form *f, LayerSpec *l) {
  static const char *const SURF[] = {"pcb", "traces", "laminate", "carbon", "brushed"};
  section(f, "LAYER");
  p_name(f, "Name", &l->name);
  p_req(f, "Thickness", &l->thickness, 0.05, "mm");
  p_num(f, "Starts at", &l->base, 0.1, "mm", 0);
  section(f, "LOOK");
  p_material(f, "Material", &l->material);
  p_color(f, "Colour", &l->color);
  p_pick(f, "Surface", &l->surface, SURF, COUNT(SURF), "Plain");
  p_glow(f, "Glow", &l->glow);
  section(f, "ABOUT");
  form_identity(f, &l->category, &l->info, &l->detail);
  p_specs(f, &l->specs, &l->has_specs);
  p_holes(f, "OPENINGS", &l->holes, &l->has_holes);
}

static void part_shape_fields(Form *f, PartSpec *p) {
  static const char *const PORTS[] = {"usbc", "hdmi", "dp"};
  static const char *const BLADES[] = {"prop"};
  static const char *const FIN[] = {"carbon", "brushed"};
  double *vs[3] = {&p->size.x, &p->size.y, &p->size.z};
  static const char *const XYZ[] = {"x", "y", "z"};
  switch (p->shape) {
  case SHAPE_BOX:
    p_nums(f, "Size", vs, XYZ, 3, 0.1, "mm");
    p_num(f, "Round", &p->round, 0.05, "mm", 0.5);
    break;
  case SHAPE_CYLINDER:
    p_num(f, "Radius", &p->radius, 0.05, "mm", 2);
    p_num(f, "Height", &p->height, 0.05, "mm", 2);
    break;
  case SHAPE_SPHERE: p_num(f, "Radius", &p->radius, 0.05, "mm", 2); break;
  case SHAPE_FAN:
    p_num(f, "Radius", &p->radius, 0.1, "mm", 10);
    p_num(f, "Height", &p->height, 0.05, "mm", 3);
    p_num(f, "Blades", &p->blades, 1, NULL, 7);
    p_pick(f, "Blade", &p->blade, BLADES, COUNT(BLADES), "Fan");
    p_tri(f, "Tip ring", &p->has_ring, &p->ring);
    p_tri(f, "Mirror", &p->has_mirror, &p->mirror);
    break;
  case SHAPE_PORT:
    p_nums(f, "Size", vs, XYZ, 3, 0.1, "mm");
    p_pick(f, "Connector", &p->port, PORTS, COUNT(PORTS), NULL);
    p_num(f, "Facing", &p->facing, 1, "\xC2\xB0", 0);
    break;
  case SHAPE_OUTLINE:
    p_num(f, "Height", &p->height, 0.05, "mm", 1);
    p_pick(f, "Finish", &p->finish, FIN, COUNT(FIN), "None");
    break;
  case SHAPE_FILE: p_text(f, "File", &p->file); break;
  default: break;
  }
}

static void form_part(Form *f, int li, int pi) {
  static const char *const SHAPES[] = {"Box", "Cylinder", "Sphere", "Fan", "Port", "Outline", "File"};
  static const char *const PATS[] = {"sm-grid", "sram", "phy", "logic"};
  static const char *const REP[] = {"Row", "Grid", "Around", "Scattered"};
  Doc *d = ed_doc(f->e);
  PartSpec *p = &f->s->layers[li].parts[pi];
  section(f, "PART");
  p_name(f, "Name", &p->name);
  int shape = (int)p->shape, was = shape;
  if (ui_dropdown(row(f, "Shape"), "Shape", &shape, SHAPES, COUNT(SHAPES)) && shape != was) {
    const char *keep_name = p->name;
    bool had_at = p->has_at;
    Dbl2 at = p->at;
    arr_free(p->points);
    p->has_size = false;
    p->has_points = false;
    spec_part_defaults(f->s, p, (Shape)shape);
    p->name = keep_name;
    p->has_at = had_at;
    p->at = at;
    changed(f);
  }
  // Which layer it sits on.
  int nl = arr_len(f->s->layers), cur = li;
  const char *names[48];
  for (int i = 0; i < nl && i < 48; i++) names[i] = f->s->layers[i].name;
  if (ui_dropdown(row(f, "On layer"), "On layer", &cur, names, mini(nl, 48)) && cur != li) {
    PartSpec mv = *p;
    arr_remove(f->s->layers[li].parts, pi);
    f->s->layers[cur].has_parts = true;
    arr_push(f->s->layers[cur].parts, mv);
    d->sel = (Sel){SEL_PART, cur, arr_len(f->s->layers[cur].parts) - 1};
    changed(f);
    return; // the form's pointers moved
  }
  section(f, "PLACE AND SIZE");
  if (ui_drag(rect(f->x + f->lw, f->y, (f->w - f->lw - S(3)) / 2, f->rh), "At x", &p->at.x, 0.1, "x")) p->has_at = true, changed(f);
  ui_text(f->x, f->y + (f->rh - ui_line_height(S(11))) / 2, S(11), FONT_REGULAR, C_DIM, "At");
  if (ui_drag(rect(f->x + f->lw + (f->w - f->lw - S(3)) / 2 + S(3), f->y, (f->w - f->lw - S(3)) / 2, f->rh), "At z", &p->at.y, 0.1, "z")) p->has_at = true, changed(f);
  f->y += f->rh + S(3);
  p_num(f, "Lift", &p->lift, 0.05, "mm", 1);
  part_shape_fields(f, p);
  section(f, "LOOK");
  p_material(f, "Material", &p->material);
  p_color(f, "Colour", &p->color);
  p_glow(f, "Glow", &p->glow);
  p_pick(f, "Pattern", &p->pattern, PATS, COUNT(PATS), "None");
  if (p->pattern) {
    if (!p->has_cells) p->cells = (Dbl2){6, 14};
    double cx = p->cells.x, cy = p->cells.y;
    p_d2(f, "Cells", &p->cells, "columns", "rows", 1, NULL);
    if (cx != p->cells.x || cy != p->cells.y) p->has_cells = true;
  }
  p_text(f, "Marking", &p->marking);
  section(f, "COPIES");
  bool on = p->repeat.on;
  if (ui_button(rect(f->x, f->y, f->w, f->rh), on ? "COPIES ON##rep" : "ONE ONLY: MAKE COPIES##rep", on ? BTN_ON : 0)) {
    p->repeat.on = !on;
    if (!on) {
      if (!is_set(p->repeat.count) || p->repeat.count < 1) p->repeat.count = 3;
      if (!p->repeat.has_step && !p->repeat.has_grid && !p->repeat.has_around && !p->has_scatter) {
        double w = p->has_size ? p->size.x : (is_set(p->radius) ? p->radius * 2 : 5);
        p->repeat.has_step = true;
        p->repeat.step = (Dbl2){w * 1.3, 0};
      }
    }
    changed(f);
  }
  f->y += f->rh + S(3);
  if (p->repeat.on) {
    p_req(f, "Count", &p->repeat.count, 1, NULL);
    int mode = p->has_scatter ? 3 : p->repeat.has_around ? 2 : p->repeat.has_grid ? 1 : 0, m0 = mode;
    if (ui_dropdown(row(f, "Arranged"), "Arranged", &mode, REP, COUNT(REP)) && mode != m0) {
      p->repeat.has_step = mode == 0 || mode == 1;
      p->repeat.has_grid = mode == 1;
      p->repeat.has_around = mode == 2;
      p->has_scatter = mode == 3;
      if (mode == 1 && p->repeat.grid.x <= 0) p->repeat.grid = (Dbl2){3, 2};
      if (mode <= 1 && p->repeat.step.x == 0 && p->repeat.step.y == 0) p->repeat.step = (Dbl2){5, 5};
      if (mode == 2 && p->repeat.around <= 0) p->repeat.around = 10;
      if (mode == 3 && p->scatter.x <= 0) p->scatter = (Dbl2){fmax(f->s->footprint.x * 0.6, 1), fmax(f->s->footprint.y * 0.6, 1)};
      changed(f);
    }
    if (mode == 0 || mode == 1) p_d2(f, "Step", &p->repeat.step, "x", "z", 0.1, "mm");
    if (mode == 1) p_d2(f, "Grid", &p->repeat.grid, "columns", "rows", 1, NULL);
    if (mode == 2) {
      if (ui_drag(row(f, "Radius##around"), "around radius", &p->repeat.around, 0.1, "mm")) changed(f);
      if (ui_drag(row(f, "Start angle"), "start angle", &p->repeat.start_angle, 1, "\xC2\xB0")) p->repeat.has_start = true, changed(f);
    }
    if (mode == 3) p_d2(f, "Area", &p->scatter, "x", "z", 0.5, "mm");
    p_names(f, &p->repeat.names);
  }
  section(f, "MOTION");
  p_value(f, "Spin", &p->spin, 100, "rpm", 3000);
  if (ui_button(rect(f->x, f->y, f->w, f->rh), p->has_pulse ? "PULSES WITH A PHASE##pulse" : "NO PULSE##pulse", p->has_pulse ? BTN_ON : 0)) {
    p->has_pulse = !p->has_pulse;
    if (p->has_pulse && !is_set(p->pulse_phase)) p->pulse_phase = 0;
    changed(f);
  }
  f->y += f->rh + S(3);
  if (p->has_pulse) {
    p_num(f, "Phase (0-2)", &p->pulse_phase, 1, NULL, 0);
    p_value(f, "Pulse level", &p->pulse_level, 0.05, NULL, 1);
    p_color(f, "Pulse colour", &p->pulse_color);
  }
  section(f, "ABOUT");
  form_identity(f, &p->category, &p->info, &p->detail);
  p_specs(f, &p->specs, &p->has_specs);
  if (p->shape == SHAPE_OUTLINE) {
    p_points(f, &p->points);
    p->has_points = true;
    p_holes(f, "CUTOUTS", &p->cutouts, &p->has_cutouts);
  }
}

static void pick_button(Form *f, Rect r, PickFor what, const char *id) {
  Editor *e = f->e;
  char lbl[64];
  snprintf(lbl, sizeof lbl, "PICK##%s", id);
  if (ui_button(r, lbl, e->pick == what ? BTN_ON : 0)) e->pick = e->pick == what ? PICK_NONE : what;
}

static const char *const WIRE_KIND_NAMES[] = {"power", "ground", "phase", "data", "signal", "video", "rf"};

static void end_row(Form *f, const char *label, const char **field, PickFor what) {
  Rect r = row(f, label);
  float bw = S(44);
  char buf[300];
  snprintf(buf, sizeof buf, "%s", *field ? *field : "");
  Rect tf = rect(r.x, r.y, r.w - bw - S(3), r.h);
  if (ui_text_field(tf, label, buf, sizeof buf, NULL)) {
    *field = spec_str(f->s, buf);
    changed(f);
  }
  Doc *d = ed_doc(f->e);
  if (d->model && *field && model_find(d->model, *field) < 0) {
    bool known = false;
    for (int i = 0; i < arr_len(d->model->nodes) && !known; i++) {
      const Node *n = &d->model->nodes[i];
      for (int k = 0; n->names && k < arr_len(n->names); k++)
        if (n->names[k] && !strcmp(n->names[k], *field)) known = true;
    }
    if (!known) ui_border(tf, C_ERROR, maxf(1, S(1)));
  }
  pick_button(f, rect(r.x + r.w - bw, r.y, bw, r.h), what, label);
}

static void form_wire(Form *f, WireSpec *w) {
  section(f, "WIRE");
  p_text(f, "Name", &w->name);
  end_row(f, "From", &w->from, PICK_WIRE_FROM);
  end_row(f, "To", &w->to, PICK_WIRE_TO);
  p_pick(f, "Carries", &w->kind, WIRE_KIND_NAMES, COUNT(WIRE_KIND_NAMES), NULL);
  p_num(f, "Gauge", &w->gauge, 0.05, "mm", 1);
  p_num(f, "Phase (0-2)", &w->phase, 1, NULL, 0);
  p_color(f, "Colour", &w->color);
  p_value(f, "Level", &w->level, 0.05, NULL, 1);
  section(f, "ABOUT");
  form_identity(f, &w->category, &w->info, NULL);
  p_specs(f, &w->specs, &w->has_specs);
  p_via(f, &w->via, &w->has_via);
}

static void form_link(Form *f, LinkSpec *n) {
  section(f, "LINK ON THE BOARD");
  p_text(f, "Name", &n->name);
  end_row(f, "From", &n->from, PICK_LINK_FROM);
  section(f, "TO");
  int del = -1;
  for (int i = 0; i < arr_len(n->to); i++) {
    ui_push_id(i);
    char buf[300];
    snprintf(buf, sizeof buf, "%s", n->to[i]);
    if (ui_text_field(rect(f->x, f->y, f->w - S(21), f->rh), "link to", buf, sizeof buf, NULL)) {
      n->to[i] = spec_str(f->s, buf);
      changed(f);
    }
    if (ui_button(rect(f->x + f->w - S(18), f->y, S(18), f->rh), "\xC3\x97##to", BTN_QUIET)) del = i;
    ui_pop_id();
    f->y += f->rh + S(3);
  }
  if (del >= 0) {
    arr_remove(n->to, del);
    changed(f);
  }
  float half = (f->w - S(3)) / 2;
  if (ui_button(rect(f->x, f->y, half, f->rh), "+ TO", 0)) {
    arr_push(n->to, spec_str(f->s, n->from ? n->from : ""));
    changed(f);
  }
  pick_button(f, rect(f->x + half + S(3), f->y, half, f->rh), PICK_LINK_TO, "linkto");
  f->y += f->rh + S(3);
  section(f, "LOOK");
  p_num(f, "Lanes", &n->lanes, 1, NULL, 6);
  p_num(f, "Width", &n->width, 0.02, "mm", 0.4);
  p_pick(f, "Carries", &n->kind, WIRE_KIND_NAMES, COUNT(WIRE_KIND_NAMES), "Default");
  p_color(f, "Colour", &n->color);
  p_value(f, "Level", &n->level, 0.05, NULL, 1);
  section(f, "ABOUT");
  form_identity(f, &n->category, &n->info, NULL);
  p_specs(f, &n->specs, &n->has_specs);
}

static void form_control(Form *f, ControlSpec *c) {
  section(f, "CONTROL");
  Rect r = row(f, "Id");
  char out[120];
  if (typed_field(f, r, "Id", c->id, out, sizeof out) && out[0]) {
    AssemblySpec *s = f->s;
    const char *old = c->id, *now = spec_str(s, out);
    // Rules follow the id wherever it goes.
#define FOLLOW(v) do { if ((v).by && old && !strcmp((v).by, old)) (v).by = now; } while (0)
    for (int i = 0; i < arr_len(s->layers); i++) {
      for (int k = 0; k < arr_len(s->layers[i].parts); k++) {
        FOLLOW(s->layers[i].parts[k].spin);
        FOLLOW(s->layers[i].parts[k].pulse_level);
      }
      for (int k = 0; k < arr_len(s->layers[i].links); k++) FOLLOW(s->layers[i].links[k].level);
    }
    for (int i = 0; i < arr_len(s->wires); i++) FOLLOW(s->wires[i].level);
    for (int i = 0; i < arr_len(s->readouts); i++)
      if (s->readouts[i].by && old && !strcmp(s->readouts[i].by, old)) s->readouts[i].by = now;
#undef FOLLOW
    c->id = now;
    changed(f);
  }
  p_text(f, "Label", &c->label);
  p_text(f, "Unit", &c->unit);
  p_req(f, "Min", &c->min, 1, NULL);
  p_req(f, "Max", &c->max, 1, NULL);
  p_req(f, "Step", &c->step, 0.1, NULL);
  p_req(f, "Starts at", &c->value, 1, NULL);
  p_long(f, "What it does", &c->info);
}

static void form_readout(Form *f, ReadoutSpec *r) {
  section(f, "READOUT");
  p_text(f, "Label", &r->label);
  p_text(f, "Unit", &r->unit);
  const char *names[24] = {0};
  int n = control_list(f->s, names, 24);
  p_pick(f, "Follows", &r->by, names, n, NULL);
  p_map(f, "Map", &r->map);
  note(f, C_FAINT, "Straight lines between the points, as control:shown. 0:0, 100:29000");
  p_req(f, "Digits", &r->digits, 1, NULL);
}

// ------------------------------------------------------------------ a tour
static void json_set_str(Arena *ar, Json *o, const char *key, const char *val) {
  Json *m = json_get(o, key);
  if (m) {
    m->type = J_STR;
    m->str = arena_str(ar, val);
    return;
  }
  json_add_str(ar, o, key, val);
}

static bool tour_field(Form *f, Json *o, const char *key, const char *label, bool lng) {
  TourDoc *t = f->e->tour;
  const char *cur = json_str(o, key, "");
  char buf[4000];
  snprintf(buf, sizeof buf, "%s", cur);
  bool ch;
  if (lng) {
    ui_text_fit(f->x, f->y, f->w, S(11), FONT_REGULAR, C_DIM, label);
    f->y += S(16);
    ch = ui_multiline(rect(f->x, f->y, f->w, S(70)), label, buf, sizeof buf);
    f->y += S(74);
  } else {
    ch = ui_text_field(row(f, label), label, buf, sizeof buf, NULL);
  }
  if (ch) {
    json_set_str(&t->ar, o, key, buf);
    hist_changed(&t->hist, ed_doc(f->e)->sel);
    t->dirty = true;
  }
  return ch;
}

static void form_tour(Form *f) {
  Json *t = f->e->tour->root;
  section(f, "TOUR");
  tour_field(f, t, "name", "Name", false);
  tour_field(f, t, "lead", "Lead", false);
  tour_field(f, t, "subtitle", "Subtitle", true);
  Json *fin = json_get(t, "finale");
  if (!fin) {
    fin = json_new(&f->e->tour->ar, J_OBJ);
    json_add(&f->e->tour->ar, t, "finale", fin);
  }
  section(f, "THE END CARD");
  tour_field(f, fin, "title", "Title", false);
  tour_field(f, fin, "text", "Text", true);
  section(f, "LEVELS");
  note(f, C_DIM, "Each level is one device. Pick one in the list to work on it, or + LEVEL to add the open device.");
  if (ui_button(rect(f->x, f->y, f->w, f->rh), "+ LEVEL", 0)) tour_add_level(f->e);
  f->y += f->rh + S(3);
}

static Json *level_at(Json *t, int i) {
  Json *lv = json_get(t, "levels");
  int k = 0;
  for (Json *l = lv ? lv->head : NULL; l; l = l->next, k++)
    if (k == i) return l;
  return NULL;
}

static void form_level(Form *f, int i) {
  Json *l = level_at(f->e->tour->root, i);
  if (!l) return;
  section(f, "TOUR LEVEL");
  tour_field(f, l, "file", "File", false);
  tour_field(f, l, "scale", "Scale", false);
  tour_field(f, l, "title", "Title", false);
  tour_field(f, l, "text", "Text", true);
  tour_field(f, l, "caption", "Caption", false);
  tour_field(f, l, "into", "Zooms into", false);
  float half = (f->w - S(3)) / 2;
  if (ui_button(rect(f->x, f->y, half, f->rh), "+ LEVEL", 0)) tour_add_level(f->e);
  if (ui_button(rect(f->x + half + S(3), f->y, half, f->rh), "REMOVE LEVEL", 0)) tour_remove_level(f->e, i);
  f->y += f->rh + S(3);
}

bool ed_tour_active(const Editor *e) { return e->tour != NULL; }

const char *ed_tour_level_file(const Editor *e, int i) {
  static char out[1400];
  Json *l = level_at(e->tour->root, i);
  if (!l) return NULL;
  Arena ar = {0};
  snprintf(out, sizeof out, "%s", path_join(&ar, e->tour->path, json_str(l, "file", "")));
  arena_free(&ar);
  return out;
}

void tour_add_level(Editor *e) {
  TourDoc *t = e->tour;
  Doc *d = ed_doc(e);
  if (!d->path[0]) {
    ed_status(e, true, "Save this device first: a level names a file");
    return;
  }
  Json *lv = json_get(t->root, "levels");
  Json *l = json_new(&t->ar, J_OBJ);
  char rel[1024];
  path_rel(t->path, d->path, rel, sizeof rel);
  json_add_str(&t->ar, l, "file", rel);
  json_add_str(&t->ar, l, "title", d->spec->name);
  json_add(&t->ar, lv, NULL, l);
  hist_changed(&t->hist, d->sel);
  t->dirty = true;
  ed_status(e, false, "Added %s as a level", rel);
}

void tour_remove_level(Editor *e, int i) {
  TourDoc *t = e->tour;
  Json *lv = json_get(t->root, "levels");
  if (!lv || lv->count <= 1) {
    ed_status(e, true, "A tour needs at least one level");
    return;
  }
  Json *prev = NULL;
  int k = 0;
  for (Json *l = lv->head; l; prev = l, l = l->next, k++) {
    if (k != i) continue;
    if (prev) prev->next = l->next;
    else lv->head = l->next;
    if (lv->tail == l) lv->tail = prev;
    lv->count--;
    break;
  }
  hist_changed(&t->hist, ed_doc(e)->sel);
  t->dirty = true;
  ed_doc(e)->sel = (Sel){SEL_TOUR, -1, -1};
}

bool ed_tour_save(Editor *e) {
  TourDoc *t = e->tour;
  char *text = json_write(t->root);
  size_t n = strlen(text);
  bool ok = file_write(t->path, text, n);
  if (ok) {
    free(t->saved);
    t->saved = text;
    t->dirty = false;
    ed_status(e, false, "Saved %s", t->path);
  } else {
    free(text);
    ed_status(e, true, "Couldn't write %s", t->path);
  }
  return ok;
}

void ed_tour_settle(Editor *e) {
  TourDoc *t = e->tour;
  if (!t || !t->hist.open || e->in->down[0] || ui_wants_keyboard()) return;
  char *text = json_write(t->root);
  hist_settle(&t->hist, text);
  t->dirty = strcmp(text, t->saved) != 0;
  free(text);
  e->a.stale = true;
}

bool ed_tour_undo(Editor *e, bool redo) {
  TourDoc *t = e->tour;
  if (!t) return false;
  if (t->hist.open) {
    char *text = json_write(t->root);
    hist_settle(&t->hist, text);
    free(text);
  }
  Doc *d = ed_doc(e);
  Sel sel;
  const char *text = redo ? hist_redo(&t->hist, &sel, d->sel) : hist_undo(&t->hist, &sel, d->sel);
  if (!text) return false;
  Arena ar = {0};
  char *copy = arena_str(&ar, text);
  Json *j = json_parse(&ar, copy, NULL, 0);
  if (!j) {
    arena_free(&ar);
    return false;
  }
  arena_free(&t->ar);
  t->ar = ar;
  t->root = j;
  t->dirty = strcmp(text, t->saved) != 0;
  d->sel = sel;
  return true;
}

// ------------------------------------------------------------------ a click in the view fills a field
void props_take_pick(Editor *e, Model *m, Hit h) {
  Doc *d = ed_doc(e);
  const Node *n = &m->nodes[h.node];
  const char *name = h.copy >= 0 && n->names && h.copy < arr_len(n->names) ? n->names[h.copy] : n->name;
  if (!name || !*name) return;
  AssemblySpec *s = d->spec;
  const char *copy = spec_str(s, name);
  if (d->sel.kind == SEL_WIRE) {
    WireSpec *w = &s->wires[d->sel.index];
    // A name the app made up keeps telling the truth; one the author wrote is left alone.
    char was[640];
    snprintf(was, sizeof was, "%s to %s", w->from ? w->from : "", w->to ? w->to : "");
    bool auto_name = w->name && !strcmp(w->name, was);
    if (e->pick == PICK_WIRE_FROM) w->from = copy;
    if (e->pick == PICK_WIRE_TO) w->to = copy;
    if (auto_name) {
      snprintf(was, sizeof was, "%s to %s", w->from ? w->from : "", w->to ? w->to : "");
      w->name = spec_str(s, was);
    }
  } else if (d->sel.kind == SEL_LINK) {
    LinkSpec *l = &s->layers[d->sel.layer].links[d->sel.index];
    if (e->pick == PICK_LINK_FROM) l->from = copy;
    if (e->pick == PICK_LINK_TO) arr_push(l->to, copy);
  }
  e->pick = PICK_NONE;
  ed_doc_changed(e);
  ed_status(e, false, "Picked %s", name);
}

// ------------------------------------------------------------------ the panel
void props_panel(Editor *e, Rect r) {
  Doc *d = ed_doc(e);
  Scroll sc = ui_scroll_begin(r, "props");
  Form f = {e, d->spec, r.x + S(12), sc.y + S(2), r.w - S(24), S(86), S(24)};
  Sel s = d->sel;
  ui_push_id((int)s.kind * 10000 + (s.layer + 1) * 100 + (s.index + 1));
  switch (s.kind) {
  case SEL_DEVICE: form_device(&f); break;
  case SEL_LAYER: form_layer(&f, &d->spec->layers[s.layer]); break;
  case SEL_PART: form_part(&f, s.layer, s.index); break;
  case SEL_LINK: form_link(&f, &d->spec->layers[s.layer].links[s.index]); break;
  case SEL_WIRE: form_wire(&f, &d->spec->wires[s.index]); break;
  case SEL_CONTROL: form_control(&f, &d->spec->controls[s.index]); break;
  case SEL_READOUT: form_readout(&f, &d->spec->readouts[s.index]); break;
  case SEL_TOUR:
    if (e->tour) form_tour(&f);
    else form_device(&f);
    break;
  case SEL_LEVEL:
    if (e->tour) form_level(&f, s.index);
    else form_device(&f);
    break;
  }
  ui_pop_id();
  ui_scroll_end(&sc, f.y + S(16));
}
