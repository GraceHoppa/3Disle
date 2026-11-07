#include "spec.h"

const char *const SHAPE_NAMES[SHAPE_COUNT] = {"box", "cylinder", "sphere", "fan", "port", "outline", "file"};

// ------------------------------------------------------------------ reading
typedef struct {
  Arena *ar;
  const Json *obj;
  Json *extra;
  const char *known[48];
  int n;
} Rd;

static Rd rd(Arena *ar, const Json *obj) { return (Rd){.ar = ar, .obj = obj}; }
static const Json *rd_take(Rd *r, const char *key) {
  r->known[r->n++] = key;
  return json_get(r->obj, key);
}
static const char *rd_str(Rd *r, const char *key) {
  const Json *m = rd_take(r, key);
  return m && m->type == J_STR ? arena_str(r->ar, m->str) : NULL;
}
static double rd_num(Rd *r, const char *key) {
  const Json *m = rd_take(r, key);
  return m && m->type == J_NUM ? m->num : UNSET;
}
static bool rd_bool(Rd *r, const char *key, bool *has) {
  const Json *m = rd_take(r, key);
  *has = m && m->type == J_BOOL;
  return *has && m->b;
}
static bool rd_d2(Rd *r, const char *key, Dbl2 *out) {
  const Json *m = rd_take(r, key);
  if (!m || m->type != J_ARR || m->count < 2) return false;
  out->x = m->head->num;
  out->y = m->head->next->num;
  return true;
}
// Whatever we don't know stays with the object, and is written back on save.
static Json *rd_done(Rd *r) {
  Json *extra = NULL;
  for (const Json *m = r->obj->head; m; m = m->next) {
    bool known = false;
    for (int i = 0; i < r->n && !known; i++) known = !strcmp(r->known[i], m->key);
    if (known) continue;
    if (!extra) extra = json_new(r->ar, J_OBJ);
    json_add(r->ar, extra, m->key, json_clone(r->ar, m));
  }
  return extra;
}
static KV *rd_specs(Rd *r, bool *has) {
  const Json *m = rd_take(r, "specs");
  KV *out = NULL;
  *has = m && m->type == J_OBJ;
  if (!*has) return NULL;
  for (const Json *e = m->head; e; e = e->next) {
    const char *v = e->type == J_STR ? e->str : "";
    arr_push(out, ((KV){arena_str(r->ar, e->key), arena_str(r->ar, v)}));
  }
  return out;
}
static Dbl2 *parse_map(const Json *m) {
  Dbl2 *out = NULL;
  if (!m || m->type != J_ARR) return NULL;
  for (const Json *e = m->head; e; e = e->next)
    if (e->type == J_ARR && e->count >= 2) arr_push(out, ((Dbl2){e->head->num, e->head->next->num}));
  return out;
}
static Value parse_value_spec(Arena *ar, const Json *m) {
  Value v = {0};
  if (!m) return v;
  if (m->type == J_NUM) {
    v.set = true;
    v.num = m->num;
  } else if (m->type == J_OBJ && json_get(m, "by")) {
    v.set = v.is_rule = true;
    v.by = arena_str(ar, json_str(m, "by", ""));
    v.map = parse_map(json_get(m, "map"));
  }
  return v;
}
static Hole *parse_holes(const Json *m) {
  Hole *out = NULL;
  if (!m || m->type != J_ARR) return NULL;
  for (const Json *e = m->head; e; e = e->next) {
    Hole h = {.radius = json_num(e, "radius", 0)};
    const Json *at = json_get(e, "at");
    if (at && at->count >= 2) h.at = (Dbl2){at->head->num, at->head->next->num};
    arr_push(out, h);
  }
  return out;
}

static PartSpec parse_part(Arena *ar, const Json *j) {
  Rd r = rd(ar, j);
  PartSpec p = {0};
  p.name = rd_str(&r, "name");
  if (!p.name) p.name = arena_str(ar, "");
  const char *shape = rd_str(&r, "shape");
  p.shape = SHAPE_BOX;
  bool shape_known = false;
  for (int i = 0; shape && i < SHAPE_COUNT; i++)
    if (!strcmp(shape, SHAPE_NAMES[i])) p.shape = (Shape)i, shape_known = true;
  if (shape && !shape_known) p.shape = SHAPE_CYLINDER; // the builder treats anything else as a cylinder
  p.info = rd_str(&r, "info");
  p.category = rd_str(&r, "category");
  p.detail = rd_str(&r, "detail");
  p.color = rd_str(&r, "color");
  p.material = rd_str(&r, "material");
  p.file = rd_str(&r, "file");
  p.pattern = rd_str(&r, "pattern");
  p.marking = rd_str(&r, "marking");
  p.finish = rd_str(&r, "finish");
  p.port = rd_str(&r, "port");
  p.blade = rd_str(&r, "blade");
  p.has_at = rd_d2(&r, "at", &p.at);
  p.has_scatter = rd_d2(&r, "scatter", &p.scatter);
  p.has_cells = rd_d2(&r, "cells", &p.cells);
  const Json *size = rd_take(&r, "size");
  if (size && size->count >= 3) {
    p.has_size = true;
    p.size = (Dbl3){size->head->num, size->head->next->num, size->head->next->next->num};
  }
  p.radius = rd_num(&r, "radius");
  p.height = rd_num(&r, "height");
  p.round = rd_num(&r, "round");
  p.lift = rd_num(&r, "lift");
  p.facing = rd_num(&r, "facing");
  p.blades = rd_num(&r, "blades");
  p.ring = rd_bool(&r, "ring", &p.has_ring);
  p.mirror = rd_bool(&r, "mirror", &p.has_mirror);
  const Json *glow = rd_take(&r, "glow");
  if (glow && glow->type == J_STR) p.glow = arena_str(ar, glow->str);
  else if (glow && glow->type == J_BOOL && glow->b) p.glow = arena_str(ar, "");
  p.spin = parse_value_spec(ar, rd_take(&r, "spin"));
  const Json *pulse = rd_take(&r, "pulse");
  p.pulse_phase = UNSET;
  if (pulse && pulse->type == J_OBJ) {
    p.has_pulse = true;
    p.pulse_phase = json_num(pulse, "phase", UNSET);
    p.pulse_level = parse_value_spec(ar, json_get(pulse, "level"));
    const char *c = json_str(pulse, "color", NULL);
    p.pulse_color = c ? arena_str(ar, c) : NULL;
  }
  const Json *pts = rd_take(&r, "points");
  p.has_points = pts != NULL;
  p.points = parse_map(pts);
  const Json *cut = rd_take(&r, "cutouts");
  p.has_cutouts = cut != NULL;
  p.cutouts = parse_holes(cut);
  const Json *rep = rd_take(&r, "repeat");
  if (rep && rep->type == J_OBJ) {
    p.repeat.on = true;
    p.repeat.count = json_num(rep, "count", UNSET);
    const Json *st = json_get(rep, "step");
    if (st && st->count >= 2) p.repeat.has_step = true, p.repeat.step = (Dbl2){st->head->num, st->head->next->num};
    const Json *gr = json_get(rep, "grid");
    if (gr && gr->count >= 2) p.repeat.has_grid = true, p.repeat.grid = (Dbl2){gr->head->num, gr->head->next->num};
    const Json *ar_ = json_get(rep, "around");
    if (ar_) p.repeat.has_around = true, p.repeat.around = ar_->num;
    const Json *sa = json_get(rep, "startAngle");
    if (sa) p.repeat.has_start = true, p.repeat.start_angle = sa->num;
    const Json *nm = json_get(rep, "names");
    if (nm && nm->type == J_ARR)
      for (const Json *e = nm->head; e; e = e->next) arr_push(p.repeat.names, arena_str(ar, e->str ? e->str : ""));
  }
  p.specs = rd_specs(&r, &p.has_specs);
  p.extra = rd_done(&r);
  return p;
}

static LinkSpec parse_link(Arena *ar, const Json *j) {
  Rd r = rd(ar, j);
  LinkSpec l = {0};
  l.name = rd_str(&r, "name");
  l.from = rd_str(&r, "from");
  if (!l.from) l.from = arena_str(ar, "");
  const Json *to = rd_take(&r, "to");
  if (to && to->type == J_STR) arr_push(l.to, arena_str(ar, to->str));
  else if (to && to->type == J_ARR) {
    l.to_is_array = true;
    for (const Json *e = to->head; e; e = e->next) arr_push(l.to, arena_str(ar, e->str ? e->str : ""));
  }
  l.color = rd_str(&r, "color");
  l.kind = rd_str(&r, "kind");
  l.info = rd_str(&r, "info");
  l.category = rd_str(&r, "category");
  l.lanes = rd_num(&r, "lanes");
  l.width = rd_num(&r, "width");
  l.level = parse_value_spec(ar, rd_take(&r, "level"));
  l.specs = rd_specs(&r, &l.has_specs);
  l.extra = rd_done(&r);
  return l;
}

static LayerSpec parse_layer(Arena *ar, const Json *j) {
  Rd r = rd(ar, j);
  LayerSpec l = {0};
  l.name = rd_str(&r, "name");
  if (!l.name) l.name = arena_str(ar, "");
  l.thickness = rd_num(&r, "thickness");
  if (!is_set(l.thickness)) l.thickness = 0;
  l.base = rd_num(&r, "base");
  l.color = rd_str(&r, "color");
  l.material = rd_str(&r, "material");
  l.info = rd_str(&r, "info");
  l.category = rd_str(&r, "category");
  l.detail = rd_str(&r, "detail");
  l.surface = rd_str(&r, "surface");
  const Json *glow = rd_take(&r, "glow");
  if (glow && glow->type == J_STR) l.glow = arena_str(ar, glow->str);
  else if (glow && glow->type == J_BOOL && glow->b) l.glow = arena_str(ar, "");
  l.specs = rd_specs(&r, &l.has_specs);
  const Json *parts = rd_take(&r, "parts");
  l.has_parts = parts != NULL;
  if (parts && parts->type == J_ARR)
    for (const Json *e = parts->head; e; e = e->next) arr_push(l.parts, parse_part(ar, e));
  const Json *links = rd_take(&r, "links");
  l.has_links = links != NULL;
  if (links && links->type == J_ARR)
    for (const Json *e = links->head; e; e = e->next) arr_push(l.links, parse_link(ar, e));
  const Json *holes = rd_take(&r, "holes");
  l.has_holes = holes != NULL;
  l.holes = parse_holes(holes);
  l.extra = rd_done(&r);
  return l;
}

static WireSpec parse_wire(Arena *ar, const Json *j) {
  Rd r = rd(ar, j);
  WireSpec w = {0};
  w.name = rd_str(&r, "name");
  w.from = rd_str(&r, "from");
  w.to = rd_str(&r, "to");
  if (!w.name) w.name = arena_str(ar, "");
  if (!w.from) w.from = arena_str(ar, "");
  if (!w.to) w.to = arena_str(ar, "");
  w.kind = rd_str(&r, "kind");
  w.color = rd_str(&r, "color");
  w.info = rd_str(&r, "info");
  w.category = rd_str(&r, "category");
  w.phase = rd_num(&r, "phase");
  w.gauge = rd_num(&r, "gauge");
  w.level = parse_value_spec(ar, rd_take(&r, "level"));
  const Json *via = rd_take(&r, "via");
  w.has_via = via != NULL;
  if (via && via->type == J_ARR)
    for (const Json *e = via->head; e; e = e->next)
      if (e->count >= 3) arr_push(w.via, ((Dbl3){e->head->num, e->head->next->num, e->head->next->next->num}));
  w.specs = rd_specs(&r, &w.has_specs);
  w.extra = rd_done(&r);
  return w;
}

AssemblySpec *spec_parse(const char *text, char *err, size_t errlen) {
  AssemblySpec *s = calloc(1, sizeof *s);
  Json *j = json_parse(&s->ar, text, err, errlen);
  if (!j || j->type != J_OBJ || !json_get(j, "layers")) {
    if (j && err) snprintf(err, errlen, "not an assembly: it has no \"layers\"");
    spec_free(s);
    return NULL;
  }
  Rd r = rd(&s->ar, j);
  s->name = rd_str(&r, "name");
  if (!s->name) s->name = arena_str(&s->ar, "Untitled");
  rd_d2(&r, "footprint", &s->footprint);
  const Json *layers = rd_take(&r, "layers");
  if (layers->type == J_ARR)
    for (const Json *e = layers->head; e; e = e->next) arr_push(s->layers, parse_layer(&s->ar, e));
  const Json *wires = rd_take(&r, "wires");
  s->has_wires = wires != NULL;
  if (wires && wires->type == J_ARR)
    for (const Json *e = wires->head; e; e = e->next) arr_push(s->wires, parse_wire(&s->ar, e));
  const Json *controls = rd_take(&r, "controls");
  s->has_controls = controls != NULL;
  if (controls && controls->type == J_ARR)
    for (const Json *e = controls->head; e; e = e->next) {
      Rd c = rd(&s->ar, e);
      ControlSpec k = {0};
      k.id = rd_str(&c, "id");
      k.label = rd_str(&c, "label");
      k.unit = rd_str(&c, "unit");
      k.info = rd_str(&c, "info");
      k.min = rd_num(&c, "min");
      k.max = rd_num(&c, "max");
      k.step = rd_num(&c, "step");
      k.value = rd_num(&c, "value");
      k.extra = rd_done(&c);
      arr_push(s->controls, k);
    }
  const Json *readouts = rd_take(&r, "readouts");
  s->has_readouts = readouts != NULL;
  if (readouts && readouts->type == J_ARR)
    for (const Json *e = readouts->head; e; e = e->next) {
      Rd c = rd(&s->ar, e);
      ReadoutSpec k = {0};
      k.label = rd_str(&c, "label");
      k.unit = rd_str(&c, "unit");
      k.by = rd_str(&c, "by");
      k.map = parse_map(rd_take(&c, "map"));
      k.digits = rd_num(&c, "digits");
      k.extra = rd_done(&c);
      arr_push(s->readouts, k);
    }
  s->extra = rd_done(&r);
  return s;
}

AssemblySpec *spec_load(const char *path, char *err, size_t errlen) {
  size_t len;
  char *text = file_read(path, &len);
  if (!text) {
    if (err) snprintf(err, errlen, "can't read %s", path);
    return NULL;
  }
  AssemblySpec *s = spec_parse(text, err, errlen);
  free(text);
  return s;
}

AssemblySpec *spec_new(const char *name, double w, double d) {
  AssemblySpec *s = calloc(1, sizeof *s);
  s->name = arena_str(&s->ar, name);
  s->footprint = (Dbl2){w, d};
  return s;
}

void spec_free(AssemblySpec *s) {
  if (!s) return;
  for (int i = 0; i < arr_len(s->layers); i++) {
    LayerSpec *l = &s->layers[i];
    for (int k = 0; k < arr_len(l->parts); k++) {
      PartSpec *p = &l->parts[k];
      arr_free(p->points);
      arr_free(p->cutouts);
      arr_free(p->repeat.names);
      arr_free(p->specs);
      arr_free(p->spin.map);
      arr_free(p->pulse_level.map);
    }
    for (int k = 0; k < arr_len(l->links); k++) {
      LinkSpec *n = &l->links[k];
      arr_free(n->to);
      arr_free(n->specs);
      arr_free(n->level.map);
    }
    arr_free(l->parts);
    arr_free(l->links);
    arr_free(l->holes);
    arr_free(l->specs);
  }
  for (int i = 0; i < arr_len(s->wires); i++) {
    arr_free(s->wires[i].via);
    arr_free(s->wires[i].specs);
    arr_free(s->wires[i].level.map);
  }
  for (int i = 0; i < arr_len(s->readouts); i++) arr_free(s->readouts[i].map);
  arr_free(s->layers);
  arr_free(s->wires);
  arr_free(s->controls);
  arr_free(s->readouts);
  arena_free(&s->ar);
  free(s);
}

// ------------------------------------------------------------------ writing
static Json *jarr(Arena *ar) { return json_new(ar, J_ARR); }
static void w_str(Arena *ar, Json *o, const char *k, const char *v) {
  if (v) json_add_str(ar, o, k, v);
}
static void w_num(Arena *ar, Json *o, const char *k, double v) {
  if (is_set(v)) json_add_num(ar, o, k, v);
}
static Json *num_pair(Arena *ar, double a, double b) {
  Json *p = jarr(ar);
  json_add_num(ar, p, NULL, a);
  json_add_num(ar, p, NULL, b);
  return p;
}
static void w_d2(Arena *ar, Json *o, const char *k, Dbl2 v) { json_add(ar, o, k, num_pair(ar, v.x, v.y)); }
static Json *map_json(Arena *ar, const Dbl2 *m) {
  Json *a = jarr(ar);
  for (int i = 0; i < arr_len(m); i++) json_add(ar, a, NULL, num_pair(ar, m[i].x, m[i].y));
  return a;
}
static void w_value(Arena *ar, Json *o, const char *k, const Value *v) {
  if (!v->set) return;
  if (!v->is_rule) {
    json_add_num(ar, o, k, v->num);
    return;
  }
  Json *r = json_new(ar, J_OBJ);
  json_add_str(ar, r, "by", v->by ? v->by : "");
  json_add(ar, r, "map", map_json(ar, v->map));
  json_add(ar, o, k, r);
}
static void w_specs(Arena *ar, Json *o, const KV *kv, bool has) {
  if (!has) return;
  Json *s = json_new(ar, J_OBJ);
  for (int i = 0; i < arr_len(kv); i++) json_add_str(ar, s, kv[i].key, kv[i].val);
  json_add(ar, o, "specs", s);
}
static void w_holes(Arena *ar, Json *o, const char *k, const Hole *h, bool has) {
  if (!has) return;
  Json *a = jarr(ar);
  for (int i = 0; i < arr_len(h); i++) {
    Json *e = json_new(ar, J_OBJ);
    json_add(ar, e, "at", num_pair(ar, h[i].at.x, h[i].at.y));
    json_add_num(ar, e, "radius", h[i].radius);
    json_add(ar, a, NULL, e);
  }
  json_add(ar, o, k, a);
}
static void w_extra(Arena *ar, Json *o, const Json *extra) {
  if (!extra) return;
  for (const Json *m = extra->head; m; m = m->next) json_add(ar, o, m->key, json_clone(ar, m));
}
static void w_glow(Arena *ar, Json *o, const char *glow) {
  if (!glow) return;
  if (*glow) json_add_str(ar, o, "glow", glow);
  else json_add_bool(ar, o, "glow", true);
}

static Json *part_json(Arena *ar, const PartSpec *p) {
  Json *o = json_new(ar, J_OBJ);
  json_add_str(ar, o, "name", p->name);
  json_add_str(ar, o, "shape", SHAPE_NAMES[p->shape]);
  if (p->has_size) {
    Json *s = jarr(ar);
    json_add_num(ar, s, NULL, p->size.x);
    json_add_num(ar, s, NULL, p->size.y);
    json_add_num(ar, s, NULL, p->size.z);
    json_add(ar, o, "size", s);
  }
  w_num(ar, o, "radius", p->radius);
  w_num(ar, o, "height", p->height);
  w_num(ar, o, "round", p->round);
  if (p->has_at) w_d2(ar, o, "at", p->at);
  w_num(ar, o, "lift", p->lift);
  w_str(ar, o, "material", p->material);
  w_str(ar, o, "color", p->color);
  w_glow(ar, o, p->glow);
  w_str(ar, o, "port", p->port);
  w_num(ar, o, "facing", p->facing);
  w_num(ar, o, "blades", p->blades);
  w_str(ar, o, "blade", p->blade);
  if (p->has_ring) json_add_bool(ar, o, "ring", p->ring);
  if (p->has_mirror) json_add_bool(ar, o, "mirror", p->mirror);
  w_value(ar, o, "spin", &p->spin);
  if (p->has_pulse) {
    Json *e = json_new(ar, J_OBJ);
    w_num(ar, e, "phase", p->pulse_phase);
    w_value(ar, e, "level", &p->pulse_level);
    w_str(ar, e, "color", p->pulse_color);
    json_add(ar, o, "pulse", e);
  }
  w_str(ar, o, "finish", p->finish);
  if (p->has_points) json_add(ar, o, "points", map_json(ar, p->points));
  w_holes(ar, o, "cutouts", p->cutouts, p->has_cutouts);
  w_str(ar, o, "file", p->file);
  w_str(ar, o, "pattern", p->pattern);
  if (p->has_cells) w_d2(ar, o, "cells", p->cells);
  w_str(ar, o, "marking", p->marking);
  if (p->has_scatter) w_d2(ar, o, "scatter", p->scatter);
  if (p->repeat.on) {
    Json *e = json_new(ar, J_OBJ);
    w_num(ar, e, "count", p->repeat.count);
    if (p->repeat.has_step) w_d2(ar, e, "step", p->repeat.step);
    if (p->repeat.has_grid) w_d2(ar, e, "grid", p->repeat.grid);
    if (p->repeat.has_around) json_add_num(ar, e, "around", p->repeat.around);
    if (p->repeat.has_start) json_add_num(ar, e, "startAngle", p->repeat.start_angle);
    if (p->repeat.names) {
      Json *n = jarr(ar);
      for (int i = 0; i < arr_len(p->repeat.names); i++) json_add_str(ar, n, NULL, p->repeat.names[i]);
      json_add(ar, e, "names", n);
    }
    json_add(ar, o, "repeat", e);
  }
  w_str(ar, o, "category", p->category);
  w_str(ar, o, "info", p->info);
  w_specs(ar, o, p->specs, p->has_specs);
  w_str(ar, o, "detail", p->detail);
  w_extra(ar, o, p->extra);
  return o;
}

static Json *link_json(Arena *ar, const LinkSpec *l) {
  Json *o = json_new(ar, J_OBJ);
  w_str(ar, o, "name", l->name);
  json_add_str(ar, o, "from", l->from);
  if (l->to_is_array || arr_len(l->to) != 1) {
    Json *a = jarr(ar);
    for (int i = 0; i < arr_len(l->to); i++) json_add_str(ar, a, NULL, l->to[i]);
    json_add(ar, o, "to", a);
  } else {
    json_add_str(ar, o, "to", l->to[0]);
  }
  w_num(ar, o, "lanes", l->lanes);
  w_num(ar, o, "width", l->width);
  w_str(ar, o, "color", l->color);
  w_str(ar, o, "kind", l->kind);
  w_value(ar, o, "level", &l->level);
  w_str(ar, o, "category", l->category);
  w_str(ar, o, "info", l->info);
  w_specs(ar, o, l->specs, l->has_specs);
  w_extra(ar, o, l->extra);
  return o;
}

Json *spec_to_json(Arena *ar, const AssemblySpec *s) {
  Json *root = json_new(ar, J_OBJ);
  json_add_str(ar, root, "name", s->name);
  w_d2(ar, root, "footprint", s->footprint);
  if (s->has_controls) {
    Json *a = jarr(ar);
    for (int i = 0; i < arr_len(s->controls); i++) {
      const ControlSpec *c = &s->controls[i];
      Json *o = json_new(ar, J_OBJ);
      w_str(ar, o, "id", c->id);
      w_str(ar, o, "label", c->label);
      w_num(ar, o, "min", c->min);
      w_num(ar, o, "max", c->max);
      w_num(ar, o, "step", c->step);
      w_num(ar, o, "value", c->value);
      w_str(ar, o, "unit", c->unit);
      w_str(ar, o, "info", c->info);
      w_extra(ar, o, c->extra);
      json_add(ar, a, NULL, o);
    }
    json_add(ar, root, "controls", a);
  }
  if (s->has_readouts) {
    Json *a = jarr(ar);
    for (int i = 0; i < arr_len(s->readouts); i++) {
      const ReadoutSpec *c = &s->readouts[i];
      Json *o = json_new(ar, J_OBJ);
      w_str(ar, o, "label", c->label);
      w_str(ar, o, "by", c->by);
      json_add(ar, o, "map", map_json(ar, c->map));
      w_str(ar, o, "unit", c->unit);
      w_num(ar, o, "digits", c->digits);
      w_extra(ar, o, c->extra);
      json_add(ar, a, NULL, o);
    }
    json_add(ar, root, "readouts", a);
  }
  Json *layers = jarr(ar);
  for (int i = 0; i < arr_len(s->layers); i++) {
    const LayerSpec *l = &s->layers[i];
    Json *o = json_new(ar, J_OBJ);
    json_add_str(ar, o, "name", l->name);
    json_add_num(ar, o, "thickness", l->thickness);
    w_num(ar, o, "base", l->base);
    w_str(ar, o, "material", l->material);
    w_str(ar, o, "color", l->color);
    w_glow(ar, o, l->glow);
    w_str(ar, o, "surface", l->surface);
    w_holes(ar, o, "holes", l->holes, l->has_holes);
    w_str(ar, o, "category", l->category);
    w_str(ar, o, "info", l->info);
    w_specs(ar, o, l->specs, l->has_specs);
    w_str(ar, o, "detail", l->detail);
    if (l->has_parts) {
      Json *a = jarr(ar);
      for (int k = 0; k < arr_len(l->parts); k++) json_add(ar, a, NULL, part_json(ar, &l->parts[k]));
      json_add(ar, o, "parts", a);
    }
    if (l->has_links) {
      Json *a = jarr(ar);
      for (int k = 0; k < arr_len(l->links); k++) json_add(ar, a, NULL, link_json(ar, &l->links[k]));
      json_add(ar, o, "links", a);
    }
    w_extra(ar, o, l->extra);
    json_add(ar, layers, NULL, o);
  }
  json_add(ar, root, "layers", layers);
  if (s->has_wires) {
    Json *a = jarr(ar);
    for (int i = 0; i < arr_len(s->wires); i++) {
      const WireSpec *w = &s->wires[i];
      Json *o = json_new(ar, J_OBJ);
      json_add_str(ar, o, "name", w->name);
      json_add_str(ar, o, "from", w->from);
      json_add_str(ar, o, "to", w->to);
      w_str(ar, o, "kind", w->kind);
      w_num(ar, o, "phase", w->phase);
      w_num(ar, o, "gauge", w->gauge);
      w_str(ar, o, "color", w->color);
      w_value(ar, o, "level", &w->level);
      if (w->has_via) {
        Json *v = jarr(ar);
        for (int k = 0; k < arr_len(w->via); k++) {
          Json *p = jarr(ar);
          json_add_num(ar, p, NULL, w->via[k].x);
          json_add_num(ar, p, NULL, w->via[k].y);
          json_add_num(ar, p, NULL, w->via[k].z);
          json_add(ar, v, NULL, p);
        }
        json_add(ar, o, "via", v);
      }
      w_str(ar, o, "category", w->category);
      w_str(ar, o, "info", w->info);
      w_specs(ar, o, w->specs, w->has_specs);
      w_extra(ar, o, w->extra);
      json_add(ar, a, NULL, o);
    }
    json_add(ar, root, "wires", a);
  }
  w_extra(ar, root, s->extra);
  return root;
}

char *spec_write(const AssemblySpec *s) {
  Arena ar = {0};
  char *out = json_write(spec_to_json(&ar, s));
  arena_free(&ar);
  return out;
}

bool spec_save(const AssemblySpec *s, const char *path) {
  char *text = spec_write(s);
  bool ok = file_write(path, text, strlen(text));
  free(text);
  return ok;
}

AssemblySpec *spec_clone(const AssemblySpec *s) {
  char *text = spec_write(s);
  AssemblySpec *c = spec_parse(text, NULL, 0);
  free(text);
  return c;
}

const char *spec_str(AssemblySpec *s, const char *text) { return arena_str(&s->ar, text); }

// ------------------------------------------------------------------ editor helpers
static bool name_taken(const AssemblySpec *s, const char *name) {
  for (int i = 0; i < arr_len(s->layers); i++) {
    if (!strcmp(s->layers[i].name, name)) return true;
    for (int k = 0; k < arr_len(s->layers[i].parts); k++)
      if (!strcmp(s->layers[i].parts[k].name, name)) return true;
  }
  return false;
}
static const char *free_name(AssemblySpec *s, const char *base) {
  char buf[128];
  for (int n = 1;; n++) {
    snprintf(buf, sizeof buf, "%s %d", base, n);
    if (!name_taken(s, buf)) return spec_str(s, buf);
  }
}

void spec_part_defaults(AssemblySpec *s, PartSpec *p, Shape shape) {
  p->shape = shape;
  p->radius = p->height = p->round = p->lift = p->facing = p->blades = p->pulse_phase = UNSET;
  (void)s;
  double span = fmin(s->footprint.x, s->footprint.y);
  double unit = span > 0 ? span / 8 : 5;
  switch (shape) {
  case SHAPE_BOX:
    p->has_size = true;
    p->size = (Dbl3){unit, unit / 4, unit};
    p->material = spec_str(s, "chip");
    break;
  case SHAPE_CYLINDER:
    p->radius = unit / 2;
    p->height = unit / 2;
    p->material = spec_str(s, "aluminium");
    break;
  case SHAPE_SPHERE:
    p->radius = unit / 2;
    p->material = spec_str(s, "steel");
    break;
  case SHAPE_FAN:
    p->radius = unit * 2;
    p->height = unit / 3;
    p->blades = 7;
    p->material = spec_str(s, "plastic");
    break;
  case SHAPE_PORT:
    p->has_size = true;
    p->size = (Dbl3){unit * 1.2, unit * 0.5, unit};
    p->port = spec_str(s, "usbc");
    break;
  case SHAPE_OUTLINE: {
    double h = unit;
    p->has_points = true;
    arr_push(p->points, ((Dbl2){-h, -h}));
    arr_push(p->points, ((Dbl2){h, -h}));
    arr_push(p->points, ((Dbl2){h, h}));
    arr_push(p->points, ((Dbl2){-h, h}));
    p->height = unit / 4;
    p->material = spec_str(s, "aluminium");
    break;
  }
  case SHAPE_FILE:
  default: break;
  }
}

PartSpec *spec_add_part(AssemblySpec *s, int layer, Shape shape) {
  if (layer < 0 || layer >= arr_len(s->layers)) return NULL;
  LayerSpec *l = &s->layers[layer];
  PartSpec p = {0};
  p.name = free_name(s, SHAPE_NAMES[shape] ? "Part" : "Part");
  p.has_at = true;
  spec_part_defaults(s, &p, shape);
  l->has_parts = true;
  arr_push(l->parts, p);
  return &arr_last(l->parts);
}

int spec_add_layer(AssemblySpec *s, int at, const char *name) {
  LayerSpec l = {0};
  l.name = name ? spec_str(s, name) : free_name(s, "Layer");
  l.thickness = 1.6;
  l.base = UNSET;
  l.material = spec_str(s, "pcb");
  if (at < 0 || at > arr_len(s->layers)) at = arr_len(s->layers);
  arr_insert(s->layers, at, l);
  return at;
}

WireSpec *spec_add_wire(AssemblySpec *s, const char *from, const char *to) {
  WireSpec w = {0};
  char buf[160];
  snprintf(buf, sizeof buf, "%s to %s", from, to);
  w.name = spec_str(s, buf);
  w.from = spec_str(s, from);
  w.to = spec_str(s, to);
  w.kind = spec_str(s, "data");
  w.phase = UNSET;
  w.gauge = 1;
  s->has_wires = true;
  arr_push(s->wires, w);
  return &arr_last(s->wires);
}

#define ARR_COPY(dst, src)                                         \
  do {                                                             \
    (dst) = NULL;                                                  \
    for (int i_ = 0; i_ < arr_len(src); i_++) arr_push((dst), (src)[i_]); \
  } while (0)

static void part_own_arrays(PartSpec *c, const PartSpec *o) {
  ARR_COPY(c->points, o->points);
  ARR_COPY(c->cutouts, o->cutouts);
  ARR_COPY(c->specs, o->specs);
  ARR_COPY(c->spin.map, o->spin.map);
  ARR_COPY(c->pulse_level.map, o->pulse_level.map);
  // Copy names are the original's: two copies answering to one name would only confuse.
  c->repeat.names = NULL;
}

PartSpec *spec_dup_part(AssemblySpec *s, int layer, int part) {
  LayerSpec *l = &s->layers[layer];
  PartSpec c = l->parts[part];
  part_own_arrays(&c, &l->parts[part]);
  c.name = free_name(s, l->parts[part].name);
  arr_insert(l->parts, part + 1, c);
  return &l->parts[part + 1];
}

int spec_dup_layer(AssemblySpec *s, int layer) {
  LayerSpec c = s->layers[layer];
  const LayerSpec *o = &s->layers[layer];
  c.parts = NULL;
  c.links = NULL;
  ARR_COPY(c.holes, o->holes);
  ARR_COPY(c.specs, o->specs);
  c.name = free_name(s, o->name);
  // Base stays automatic in the copy, or it would land inside the original.
  c.base = UNSET;
  // Into the document first, empty, so each new name sees the ones before it.
  arr_insert(s->layers, layer + 1, c);
  for (int i = 0; i < arr_len(s->layers[layer].parts); i++) {
    const PartSpec *orig = &s->layers[layer].parts[i];
    PartSpec p = *orig;
    part_own_arrays(&p, orig);
    p.name = free_name(s, orig->name);
    arr_push(s->layers[layer + 1].parts, p);
  }
  s->layers[layer + 1].has_parts = s->layers[layer].has_parts;
  return layer + 1;
}

WireSpec *spec_dup_wire(AssemblySpec *s, int wire) {
  WireSpec c = s->wires[wire];
  const WireSpec *o = &s->wires[wire];
  ARR_COPY(c.via, o->via);
  ARR_COPY(c.specs, o->specs);
  ARR_COPY(c.level.map, o->level.map);
  arr_insert(s->wires, wire + 1, c);
  return &s->wires[wire + 1];
}

static void free_part(PartSpec *p) {
  arr_free(p->points);
  arr_free(p->cutouts);
  arr_free(p->repeat.names);
  arr_free(p->specs);
  arr_free(p->spin.map);
  arr_free(p->pulse_level.map);
}
static void free_link(LinkSpec *n) {
  arr_free(n->to);
  arr_free(n->specs);
  arr_free(n->level.map);
}
void spec_remove_part(AssemblySpec *s, int layer, int part) {
  LayerSpec *l = &s->layers[layer];
  free_part(&l->parts[part]);
  arr_remove(l->parts, part);
}
void spec_remove_link(AssemblySpec *s, int layer, int link) {
  LayerSpec *l = &s->layers[layer];
  free_link(&l->links[link]);
  arr_remove(l->links, link);
}
void spec_remove_layer(AssemblySpec *s, int layer) {
  LayerSpec *l = &s->layers[layer];
  for (int i = 0; i < arr_len(l->parts); i++) free_part(&l->parts[i]);
  for (int i = 0; i < arr_len(l->links); i++) free_link(&l->links[i]);
  arr_free(l->parts);
  arr_free(l->links);
  arr_free(l->holes);
  arr_free(l->specs);
  arr_remove(s->layers, layer);
}
void spec_remove_wire(AssemblySpec *s, int wire) {
  WireSpec *w = &s->wires[wire];
  arr_free(w->via);
  arr_free(w->specs);
  arr_free(w->level.map);
  arr_remove(s->wires, wire);
}
void spec_remove_readout(AssemblySpec *s, int readout) {
  arr_free(s->readouts[readout].map);
  arr_remove(s->readouts, readout);
}
bool spec_name_taken(const AssemblySpec *s, const char *name) { return name_taken(s, name); }

// ------------------------------------------------------------------ logic
double lookup(const Dbl2 *m, double x) {
  int n = arr_len(m);
  if (!n || !isfinite(x)) return 0;
  if (x <= m[0].x) return m[0].y;
  for (int i = 1; i < n; i++) {
    double x0 = m[i - 1].x, y0 = m[i - 1].y, x1 = m[i].x, y1 = m[i].y;
    if (x <= x1) return x1 == x0 ? y1 : y0 + (y1 - y0) * (x - x0) / (x1 - x0);
  }
  return m[n - 1].y;
}

double value_eval(const Value *v, const ControlValue *values, int n, double fallback) {
  if (!v->set) return fallback;
  if (!v->is_rule) return v->num;
  double x = 0;
  for (int i = 0; i < n; i++)
    if (!strcmp(values[i].id, v->by)) x = values[i].value;
  return lookup(v->map, x);
}

void format_readout(char *out, size_t len, double value, const char *unit, int digits) {
  char num[64], grouped[96];
  snprintf(num, sizeof num, "%.*f", digits, fabs(value));
  const char *dot = strchr(num, '.');
  int ints = dot ? (int)(dot - num) : (int)strlen(num), g = 0;
  if (value < 0) g += snprintf(grouped + g, sizeof grouped - (size_t)g, "\xE2\x88\x92");
  for (int i = 0; i < ints; i++) {
    grouped[g++] = num[i];
    if ((ints - i - 1) % 3 == 0 && i != ints - 1) grouped[g++] = ' ';
  }
  grouped[g] = 0;
  if (dot) strncat(grouped, dot, sizeof grouped - strlen(grouped) - 1);
  if (unit && *unit) snprintf(out, len, "%s %s", grouped, unit);
  else snprintf(out, len, "%s", grouped);
}
