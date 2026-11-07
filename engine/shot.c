#include "shot.h"

const char *const EASE_NAMES[EASE_COUNT] = {"smooth", "linear", "hold", "out", "in"};
const char *const TRACK_NAMES[TRACK_KIND_COUNT] = {"camera", "explode", "fade", "flow", "control", "offset", "opacity", "glow"};

const char *shot_str(Shot *s, const char *text) { return text ? arena_str(&s->ar, text) : NULL; }

Shot *shot_new(const char *name, const char *path) {
  Shot *s = calloc(1, sizeof *s);
  s->name = arena_str(&s->ar, name);
  s->fps = 60;
  s->w = 2560;
  s->h = 1440;
  s->duration = 10;
  snprintf(s->path, sizeof s->path, "%s", path ? path : "");
  return s;
}

void shot_free(Shot *s) {
  if (!s) return;
  for (int i = 0; i < arr_len(s->tracks); i++) arr_free(s->tracks[i].keys);
  arr_free(s->tracks);
  arr_free(s->scenes);
  arr_free(s->labels);
  arr_free(s->captions);
  arena_free(&s->ar);
  free(s);
}

static void read3(const Json *a, double out[3]) {
  if (!a || a->type != J_ARR || a->count < 3) return;
  out[0] = a->head->num;
  out[1] = a->head->next->num;
  out[2] = a->head->next->next->num;
}
static Ease ease_named(const char *n) {
  for (int i = 0; n && i < EASE_COUNT; i++)
    if (!strcmp(EASE_NAMES[i], n)) return (Ease)i;
  return EASE_SMOOTH;
}
static int cmp_key(const void *a, const void *b) {
  double x = ((const Key *)a)->t, y = ((const Key *)b)->t;
  return x < y ? -1 : x > y;
}

Shot *shot_parse(const char *text, const char *path, char *err, size_t errlen) {
  Arena tmp = {0};
  Json *j = json_parse(&tmp, text, err, errlen);
  if (!j || j->type != J_OBJ || !json_get(j, "scenes")) {
    if (j && err) snprintf(err, errlen, "not a shot: it has no \"scenes\"");
    arena_free(&tmp);
    return NULL;
  }
  Shot *s = shot_new(json_str(j, "name", "Untitled"), path);
  s->fps = (int)json_num(j, "fps", 60);
  const Json *size = json_get(j, "size");
  if (size && size->count >= 2) s->w = (int)size->head->num, s->h = (int)size->head->next->num;
  // Even, as the video format needs.
  s->w &= ~1, s->h &= ~1;
  if (s->fps < 1) s->fps = 60;
  if (s->w < 2) s->w = 2560;
  if (s->h < 2) s->h = 1440;
  s->duration = json_num(j, "duration", 0);
  const Json *a = json_get(j, "scenes");
  for (const Json *e = a->head; e; e = e->next)
    arr_push(s->scenes, ((ShotScene){shot_str(s, json_str(e, "file", "")), json_num(e, "start", 0), json_num(e, "end", 0)}));
  a = json_get(j, "tracks");
  for (const Json *e = a ? a->head : NULL; e; e = e->next) {
    Track tr = {0};
    const char *kind = json_str(e, "type", "");
    tr.kind = TRACK_KIND_COUNT;
    for (int i = 0; i < TRACK_KIND_COUNT; i++)
      if (!strcmp(TRACK_NAMES[i], kind)) tr.kind = (TrackKind)i;
    if (tr.kind == TRACK_KIND_COUNT) continue;
    tr.scene = (int)json_num(e, "scene", -1);
    tr.name = shot_str(s, json_str(e, "name", NULL));
    const Json *keys = json_get(e, "keys");
    for (const Json *k = keys ? keys->head : NULL; k; k = k->next) {
      Key key = {.t = json_num(k, "t", 0), .fov = json_num(k, "fov", 40), .ease = ease_named(json_str(k, "ease", NULL))};
      const Json *v = json_get(k, "v");
      if (v && v->type == J_NUM) key.v[0] = v->num;
      else read3(v, key.v);
      read3(json_get(k, "pos"), key.v);
      read3(json_get(k, "target"), key.target);
      arr_push(tr.keys, key);
    }
    if (tr.keys) qsort(tr.keys, (size_t)arr_len(tr.keys), sizeof(Key), cmp_key);
    arr_push(s->tracks, tr);
  }
  a = json_get(j, "labels");
  for (const Json *e = a ? a->head : NULL; e; e = e->next)
    arr_push(s->labels, ((ShotLabel){json_num(e, "t0", 0), json_num(e, "t1", 0), (int)json_num(e, "scene", 0),
                                     shot_str(s, json_str(e, "part", "")), shot_str(s, json_str(e, "text", NULL))}));
  a = json_get(j, "captions");
  for (const Json *e = a ? a->head : NULL; e; e = e->next)
    arr_push(s->captions, ((ShotCaption){json_num(e, "t0", 0), json_num(e, "t1", 0), shot_str(s, json_str(e, "scale", NULL)),
                                         shot_str(s, json_str(e, "title", NULL)), shot_str(s, json_str(e, "text", NULL)),
                                         shot_str(s, json_str(e, "caption", NULL))}));
  arena_free(&tmp);
  shot_recompute_duration(s);
  return s;
}

Shot *shot_load(const char *path, char *err, size_t errlen) {
  char *text = file_read(path, NULL);
  if (!text) {
    if (err) snprintf(err, errlen, "can't read %s", path);
    return NULL;
  }
  Shot *s = shot_parse(text, path, err, errlen);
  free(text);
  return s;
}

static Json *jv3(Arena *ar, const double v[3]) {
  Json *a = json_new(ar, J_ARR);
  for (int i = 0; i < 3; i++) json_add_num(ar, a, NULL, round(v[i] * 1000) / 1000);
  return a;
}

char *shot_write(const Shot *s) {
  Arena ar = {0};
  Json *o = json_new(&ar, J_OBJ);
  json_add_str(&ar, o, "name", s->name);
  json_add_num(&ar, o, "fps", s->fps);
  Json *size = json_new(&ar, J_ARR);
  json_add_num(&ar, size, NULL, s->w);
  json_add_num(&ar, size, NULL, s->h);
  json_add(&ar, o, "size", size);
  json_add_num(&ar, o, "duration", s->duration);
  Json *scenes = json_new(&ar, J_ARR);
  for (int i = 0; i < arr_len(s->scenes); i++) {
    Json *e = json_new(&ar, J_OBJ);
    json_add_str(&ar, e, "file", s->scenes[i].file);
    json_add_num(&ar, e, "start", s->scenes[i].start);
    json_add_num(&ar, e, "end", s->scenes[i].end);
    json_add(&ar, scenes, NULL, e);
  }
  json_add(&ar, o, "scenes", scenes);
  Json *tracks = json_new(&ar, J_ARR);
  for (int i = 0; i < arr_len(s->tracks); i++) {
    const Track *t = &s->tracks[i];
    Json *e = json_new(&ar, J_OBJ);
    json_add_str(&ar, e, "type", TRACK_NAMES[t->kind]);
    if (t->scene >= 0) json_add_num(&ar, e, "scene", t->scene);
    if (t->name) json_add_str(&ar, e, "name", t->name);
    Json *keys = json_new(&ar, J_ARR);
    for (int k = 0; k < arr_len(t->keys); k++) {
      const Key *key = &t->keys[k];
      Json *ke = json_new(&ar, J_OBJ);
      json_add_num(&ar, ke, "t", round(key->t * 10000) / 10000);
      if (t->kind == TRACK_CAMERA) {
        json_add(&ar, ke, "pos", jv3(&ar, key->v));
        json_add(&ar, ke, "target", jv3(&ar, key->target));
        if (key->fov != 40) json_add_num(&ar, ke, "fov", key->fov);
      } else if (t->kind == TRACK_OFFSET) {
        json_add(&ar, ke, "v", jv3(&ar, key->v));
      } else {
        json_add_num(&ar, ke, "v", round(key->v[0] * 10000) / 10000);
      }
      if (key->ease != EASE_SMOOTH) json_add_str(&ar, ke, "ease", EASE_NAMES[key->ease]);
      json_add(&ar, keys, NULL, ke);
    }
    json_add(&ar, e, "keys", keys);
    json_add(&ar, tracks, NULL, e);
  }
  json_add(&ar, o, "tracks", tracks);
  if (arr_len(s->labels)) {
    Json *a = json_new(&ar, J_ARR);
    for (int i = 0; i < arr_len(s->labels); i++) {
      const ShotLabel *l = &s->labels[i];
      Json *e = json_new(&ar, J_OBJ);
      json_add_num(&ar, e, "t0", l->t0);
      json_add_num(&ar, e, "t1", l->t1);
      json_add_num(&ar, e, "scene", l->scene);
      json_add_str(&ar, e, "part", l->part);
      if (l->text) json_add_str(&ar, e, "text", l->text);
      json_add(&ar, a, NULL, e);
    }
    json_add(&ar, o, "labels", a);
  }
  if (arr_len(s->captions)) {
    Json *a = json_new(&ar, J_ARR);
    for (int i = 0; i < arr_len(s->captions); i++) {
      const ShotCaption *c = &s->captions[i];
      Json *e = json_new(&ar, J_OBJ);
      json_add_num(&ar, e, "t0", c->t0);
      json_add_num(&ar, e, "t1", c->t1);
      if (c->scale) json_add_str(&ar, e, "scale", c->scale);
      if (c->title) json_add_str(&ar, e, "title", c->title);
      if (c->text) json_add_str(&ar, e, "text", c->text);
      if (c->caption) json_add_str(&ar, e, "caption", c->caption);
      json_add(&ar, a, NULL, e);
    }
    json_add(&ar, o, "captions", a);
  }
  char *out = json_write(o);
  arena_free(&ar);
  return out;
}

bool shot_save(const Shot *s, const char *path) {
  char *t = shot_write(s);
  bool ok = file_write(path, t, strlen(t));
  free(t);
  return ok;
}

void shot_recompute_duration(Shot *s) {
  double d = s->duration;
  for (int i = 0; i < arr_len(s->scenes); i++) d = fmax(d, s->scenes[i].end);
  for (int i = 0; i < arr_len(s->tracks); i++) {
    int n = arr_len(s->tracks[i].keys);
    if (n) d = fmax(d, s->tracks[i].keys[n - 1].t);
  }
  s->duration = d;
}

int shot_scene_at(const Shot *s, double t) {
  int found = -1;
  for (int i = 0; i < arr_len(s->scenes); i++)
    if (t >= s->scenes[i].start && t < s->scenes[i].end) found = i;
  if (found < 0 && arr_len(s->scenes)) {
    // Past the end stays on the last frame; before the start, on the first.
    double best = INFINITY;
    for (int i = 0; i < arr_len(s->scenes); i++) {
      double d = t >= s->scenes[i].end ? t - s->scenes[i].end : s->scenes[i].start - t;
      if (d <= best) best = d, found = i;
    }
  }
  return found;
}

Track *shot_track(Shot *s, TrackKind kind, int scene, const char *name, bool create) {
  for (int i = 0; i < arr_len(s->tracks); i++) {
    Track *t = &s->tracks[i];
    if (t->kind != kind || t->scene != scene) continue;
    if ((!t->name && !name) || (t->name && name && !strcmp(t->name, name))) return t;
  }
  if (!create) return NULL;
  arr_push(s->tracks, ((Track){kind, scene, shot_str(s, name), NULL}));
  return &arr_last(s->tracks);
}

int track_set_key(Track *tr, Key k) {
  for (int i = 0; i < arr_len(tr->keys); i++) {
    if (fabs(tr->keys[i].t - k.t) < 1e-4) {
      tr->keys[i] = k;
      return i;
    }
    if (tr->keys[i].t > k.t) {
      arr_insert(tr->keys, i, k);
      return i;
    }
  }
  arr_push(tr->keys, k);
  return arr_len(tr->keys) - 1;
}

float ease_apply(Ease e, float k) {
  k = clampf(k, 0, 1);
  switch (e) {
  case EASE_LINEAR: return k;
  case EASE_HOLD: return 0;
  case EASE_OUT: return 1 - (1 - k) * (1 - k) * (1 - k);
  case EASE_IN: return k * k * k;
  case EASE_SMOOTH:
  case EASE_COUNT: break;
  }
  return k < 0.5f ? 4 * k * k * k : 1 - powf(-2 * k + 2, 3) / 2;
}

// The keys around t, and how far between them.
static float keys_around(const Key *keys, int n, double t, int *a, int *b) {
  if (t <= keys[0].t) {
    *a = *b = 0;
    return 0;
  }
  if (t >= keys[n - 1].t) {
    *a = *b = n - 1;
    return 0;
  }
  int i = 0;
  while (i + 1 < n && keys[i + 1].t <= t) i++;
  *a = i, *b = i + 1;
  double span = keys[i + 1].t - keys[i].t;
  return span > 0 ? ease_apply(keys[i].ease, (float)((t - keys[i].t) / span)) : 1;
}

double track_value(const Track *tr, double t, double fallback) {
  int n = tr ? arr_len(tr->keys) : 0;
  if (!n) return fallback;
  int a, b;
  float k = keys_around(tr->keys, n, t, &a, &b);
  return tr->keys[a].v[0] + (tr->keys[b].v[0] - tr->keys[a].v[0]) * (double)k;
}

void track_vec3(const Track *tr, double t, double out[3]) {
  int n = tr ? arr_len(tr->keys) : 0;
  if (!n) {
    out[0] = out[1] = out[2] = 0;
    return;
  }
  int a, b;
  float k = keys_around(tr->keys, n, t, &a, &b);
  for (int i = 0; i < 3; i++) out[i] = tr->keys[a].v[i] + (tr->keys[b].v[i] - tr->keys[a].v[i]) * (double)k;
}

// The camera swings around what it looks at rather than cutting straight through the model.
bool track_camera(const Track *tr, double t, Vec3 *pos, Vec3 *target, float *fov) {
  int n = tr ? arr_len(tr->keys) : 0;
  if (!n) return false;
  int a, b;
  float k = keys_around(tr->keys, n, t, &a, &b);
  const Key *A = &tr->keys[a], *B = &tr->keys[b];
  Vec3 pa = v3((float)A->v[0], (float)A->v[1], (float)A->v[2]), pb = v3((float)B->v[0], (float)B->v[1], (float)B->v[2]);
  Vec3 ta = v3((float)A->target[0], (float)A->target[1], (float)A->target[2]), tb = v3((float)B->target[0], (float)B->target[1], (float)B->target[2]);
  Vec3 tg = v3lerp(ta, tb, k);
  Vec3 oa = v3sub(pa, ta), ob = v3sub(pb, tb);
  float da = v3len(oa), db = v3len(ob);
  Vec3 ua = da > 0 ? v3scale(oa, 1 / da) : v3(0, 0, 1), ub = db > 0 ? v3scale(ob, 1 / db) : v3(0, 0, 1);
  float cs = clampf(v3dot(ua, ub), -1, 1), om = acosf(cs);
  Vec3 dir;
  if (om < 1e-3f || om > PI - 1e-3f) {
    dir = v3norm(v3lerp(ua, ub, k));
  } else {
    float s = sinf(om);
    dir = v3add(v3scale(ua, sinf((1 - k) * om) / s), v3scale(ub, sinf(k * om) / s));
  }
  float dist = da > 0 && db > 0 ? da * powf(db / da, k) : lerpf(da, db, k);
  *target = tg;
  *pos = v3mad(tg, dir, dist);
  *fov = lerpf((float)A->fov, (float)B->fov, k);
  return true;
}

// ------------------------------------------------------------------ playing
float fit_radius_distance(float radius, float fov_deg, float aspect) {
  float vfov = fov_deg * PI / 180;
  float hfov = 2 * atanf(tanf(vfov / 2) * aspect);
  return radius / sinf(minf(vfov, hfov) / 2);
}

void default_camera(const Model *m, float aspect, float from, Vec3 *pos, Vec3 *target) {
  float dist = fit_radius_distance(m->radius, 40, aspect) * 1.15f;
  Vec3 dir = v3norm(v3(1, 0.7f, 1.2f));
  *target = m->center;
  *pos = v3mad(m->center, dir, dist * from);
}

ShotPlayer *player_new(Shot *s, BuildOpts opts, char *err, size_t errlen) { return player_new_over(s, opts, NULL, 0, err, errlen); }

ShotPlayer *player_new_over(Shot *s, BuildOpts opts, const SpecOverride *over, int nover, char *err, size_t errlen) {
  ShotPlayer *p = calloc(1, sizeof *p);
  p->shot = s;
  p->cur = -1;
  Arena ar = {0};
  for (int i = 0; i < arr_len(s->scenes); i++) {
    const char *full = path_join(&ar, s->path, s->scenes[i].file);
    AssemblySpec *sp = NULL;
    for (int k = 0; k < nover && !sp; k++)
      if (path_same(over[k].path, full)) sp = spec_clone(over[k].spec);
    if (!sp) sp = spec_load(full, err, errlen);
    if (!sp) {
      arena_free(&ar);
      player_free(p);
      return NULL;
    }
    arr_push(p->specs, sp);
    arr_push(p->models, model_build(sp, opts));
  }
  arena_free(&ar);
  return p;
}

void player_free(ShotPlayer *p) {
  if (!p) return;
  for (int i = 0; i < arr_len(p->models); i++) {
    model_free(p->models[i]);
    spec_free(p->specs[i]);
  }
  arr_free(p->models);
  arr_free(p->specs);
  free(p);
}

void player_invalidate(ShotPlayer *p) { p->cur = -1; }

#define STEP_HZ 240.0

static void apply_values(ShotPlayer *p, Model *m, int scene, double t) {
  const Shot *s = p->shot;
  for (int i = 0; i < arr_len(s->tracks); i++) {
    const Track *tr = &s->tracks[i];
    if (tr->kind == TRACK_CONTROL && tr->scene == scene && tr->name && arr_len(tr->keys))
      model_set_value(m, tr->name, track_value(tr, t, 0));
    if (tr->kind == TRACK_FLOW) m->flow_speed = (float)track_value(tr, t, 1);
  }
}

ShotFrame player_seek(ShotPlayer *p, double t, float aspect) {
  ShotFrame f = {0};
  f.scene = -1;
  Shot *s = p->shot;
  int scene = shot_scene_at(s, t);
  if (scene < 0 || scene >= arr_len(p->models)) {
    f.fade = 1;
    return f;
  }
  Model *m = p->models[scene];
  double start = s->scenes[scene].start;
  // Spins and flows are the sum of everything before now, always on the same grid of steps.
  long target = (long)floor((fmax(t, start) - start) * STEP_HZ + 1e-6);
  long done = (long)floor((p->at - start) * STEP_HZ + 1e-6);
  if (scene != p->cur || t < p->at) {
    model_reset_anim(m);
    model_start_values(m, NULL, 0);
    done = 0;
    p->cur = scene;
  }
  for (long i = done; i < target; i++) {
    apply_values(p, m, scene, start + (double)i / STEP_HZ);
    model_tick(m, (float)(1.0 / STEP_HZ));
  }
  p->at = start + (double)target / STEP_HZ;
  apply_values(p, m, scene, t);
  model_start_values(m, m->values, arr_len(m->values));

  // Everything else is simply read at t.
  float explode = 0;
  for (int i = 0; i < arr_len(m->nodes); i++) {
    Node *n = &m->nodes[i];
    n->off_anim = v3(0, 0, 0);
    n->opacity = 1;
    n->glow_anim = 0;
  }
  f.fade = 0;
  bool have_cam = false;
  float fov = 40;
  for (int i = 0; i < arr_len(s->tracks); i++) {
    const Track *tr = &s->tracks[i];
    if (tr->kind == TRACK_FADE) f.fade = (float)track_value(tr, t, 0);
    if (tr->scene != scene) continue;
    switch (tr->kind) {
    case TRACK_CAMERA: have_cam = track_camera(tr, t, &f.cam_pos, &f.cam_target, &fov); break;
    case TRACK_EXPLODE: explode = (float)track_value(tr, t, 0); break;
    case TRACK_OFFSET:
    case TRACK_OPACITY:
    case TRACK_GLOW: {
      int id = tr->name ? model_find(m, tr->name) : -1;
      if (id < 0) break;
      Node *n = &m->nodes[id];
      if (tr->kind == TRACK_OFFSET) {
        double v[3];
        track_vec3(tr, t, v);
        n->off_anim = v3((float)v[0], (float)v[1], (float)v[2]);
      } else if (tr->kind == TRACK_OPACITY) {
        n->opacity = (float)track_value(tr, t, 1);
      } else {
        n->glow_anim = (float)track_value(tr, t, 0);
        n->glow_anim_color = hex_linear("#76b900");
      }
      break;
    }
    default: break;
    }
  }
  model_set_explode(m, explode);
  model_update(m);
  if (!have_cam) default_camera(m, aspect, 1, &f.cam_pos, &f.cam_target);
  f.model = m;
  f.scene = scene;
  f.fov = fov;
  float dist = v3dist(f.cam_pos, f.cam_target);
  // Depth range from where the camera is, so close-ups and far arrivals both hold.
  f.near_z = maxf(minf(m->radius, dist) / 100, 1e-4f);
  f.far_z = maxf(dist, m->radius) * 100;
  return f;
}

// ------------------------------------------------------------------ writing a video automatically
typedef struct {
  int node;
  float size;
} Big;
static int cmp_big(const void *a, const void *b) {
  float x = ((const Big *)a)->size, y = ((const Big *)b)->size;
  return x < y ? 1 : x > y ? -1 : 0;
}

static void cam_key(Track *tr, double t, Vec3 pos, Vec3 target, Ease e) {
  Key k = {.t = t, .fov = 40, .ease = e};
  k.v[0] = pos.x, k.v[1] = pos.y, k.v[2] = pos.z;
  k.target[0] = target.x, k.target[1] = target.y, k.target[2] = target.z;
  track_set_key(tr, k);
}
static void val_key(Track *tr, double t, double v, Ease e) {
  Key k = {.t = t, .fov = 40, .ease = e};
  k.v[0] = v;
  track_set_key(tr, k);
}

static Vec3 orbit(Vec3 center, float dist, float deg, float height) {
  // Around the default view, by degrees.
  Vec3 d = v3norm(v3(1, 0.7f, 1.2f));
  float a = deg * PI / 180, c = cosf(a), s = sinf(a);
  Vec3 r = v3(d.x * c + d.z * s, d.y * height, -d.x * s + d.z * c);
  return v3mad(center, v3norm(r), dist);
}

static const char *one_line(Shot *s, const char *text, int limit) {
  // The first sentence or so: captions are read at a glance.
  if (!text) return NULL;
  char buf[512];
  snprintf(buf, sizeof buf, "%s", text);
  char *dot = NULL;
  for (char *c = buf; *c; c++)
    if ((*c == '.' || *c == ':') && (c[1] == ' ' || !c[1]) && c - buf > 20) {
      dot = c;
      break;
    }
  if (dot) dot[1] = 0;
  if ((int)strlen(buf) > limit) {
    buf[limit] = 0;
    char *sp = strrchr(buf, ' ');
    if (sp) *sp = 0;
    strcat(buf, "\xE2\x80\xA6");
  }
  return shot_str(s, buf);
}

// One device's stretch of the video. Returns where it ends.
static double auto_scene(Shot *s, int scene, Model *m, const char *scale, const char *title, const char *text, const char *caption, double t0, bool arrive_far, const char *into, bool last) {
  float aspect = 16.f / 9;
  float dist = fit_radius_distance(m->radius, 40, aspect) * 1.15f;
  Vec3 c = m->center;
  // The pointers below stay good only while the list doesn't grow past this.
  arr_reserve(s->tracks, arr_len(s->tracks) + 8);
  Track *cam = shot_track(s, TRACK_CAMERA, scene, NULL, true);
  Track *exp = shot_track(s, TRACK_EXPLODE, scene, NULL, true);
  double t = t0;
  // Arrive, then keep drifting: nothing sits dead still.
  cam_key(cam, t, orbit(c, dist * (arrive_far ? 5 : 1.5f), arrive_far ? -8 : -35, 1), c, EASE_OUT);
  t += arrive_far ? 1.8 : 3;
  cam_key(cam, t, orbit(c, dist, 0, 1), c, EASE_LINEAR);
  arr_push(s->captions, ((ShotCaption){t0 + 0.4, t + 5.5, shot_str(s, scale), shot_str(s, title), one_line(s, text, 220), shot_str(s, caption)}));
  // Biggest parts, for labels and close-ups.
  Big *big = NULL;
  for (int i = 1; i < arr_len(m->nodes); i++) {
    const Node *n = &m->nodes[i];
    if (!n->name[0] || (n->flags & (NF_SLAB | NF_WIRES | NF_WIRE | NF_SCATTER | NF_DECAL | NF_FLOW | NF_LAYER))) continue;
    if (box_is_empty(n->box)) continue;
    bool dup = false;
    for (int k = 0; k < arr_len(big) && !dup; k++) dup = !strcmp(m->nodes[big[k].node].name, n->name);
    if (dup) continue;
    arr_push(big, ((Big){i, v3len(box_size(n->box))}));
  }
  if (big) qsort(big, (size_t)arr_len(big), sizeof(Big), cmp_big);
  double drift = 5;
  cam_key(cam, t + drift, orbit(c, dist * 0.98f, 16, 1), c, EASE_SMOOTH);
  t += drift;
  // Come apart, labels on.
  if (arr_len(m->pieces) > 1) {
    val_key(exp, t - 1, 0, EASE_SMOOTH);
    val_key(exp, t + 2.2, 0.85, EASE_LINEAR);
    cam_key(cam, t + 2.2, orbit(c, dist * 1.05f, 30, 1.05f), c, EASE_LINEAR);
    int labels = mini(arr_len(big), 7);
    for (int i = 0; i < labels; i++)
      arr_push(s->labels, ((ShotLabel){t + 1.2 + 0.18 * i, t + 6.6, scene, shot_str(s, m->nodes[big[i].node].name), NULL}));
    cam_key(cam, t + 6.6, orbit(c, dist * 1.02f, 52, 1.05f), c, EASE_SMOOTH);
    val_key(exp, t + 6.6, 0.85, EASE_SMOOTH);
    t += 6.6;
  }
  // A close look at the biggest parts that say something about themselves.
  int shown = 0;
  float deg = 60;
  for (int i = 0; i < arr_len(big) && shown < 2; i++) {
    const Node *n = &m->nodes[big[i].node];
    if (!n->info || !*n->info) continue;
    float rr = box_radius(n->box);
    if (rr < m->radius * 0.06f) continue;
    Vec3 pc = box_center(n->box);
    // The part as it sits now, spread with the rest.
    int piece = node_top_piece(m, big[i].node);
    float lift = piece >= 0 ? m->spread_dir[piece * 3 + 1] * m->spread_gap * 0.85f : 0;
    pc.y += lift;
    float pd = fit_radius_distance(rr, 40, aspect) * 1.6f;
    cam_key(cam, t + 1.6, orbit(pc, pd, deg, 1.1f), pc, EASE_LINEAR);
    cam_key(cam, t + 4.6, orbit(pc, pd * 0.92f, deg + 14, 1.1f), pc, EASE_SMOOTH);
    arr_push(s->labels, ((ShotLabel){t + 1, t + 4.8, scene, shot_str(s, n->name), NULL}));
    arr_push(s->captions, ((ShotCaption){t + 1.1, t + 4.8, NULL, shot_str(s, n->name), one_line(s, n->info, 180), NULL}));
    t += 4.6;
    deg += 30;
    shown++;
  }
  // Back together, and on to whatever comes next.
  if (arr_len(m->pieces) > 1) {
    cam_key(cam, t + 2, orbit(c, dist, deg, 1), c, EASE_LINEAR);
    val_key(exp, t + 0.3, 0.85, EASE_SMOOTH);
    val_key(exp, t + 2.3, 0, EASE_SMOOTH);
    t += 2.4;
  }
  arr_free(big);
  if (into) {
    int id = model_find(m, into);
    Box b = id >= 0 ? m->nodes[id].box : m->box;
    Vec3 pc = box_center(b);
    float rr = box_radius(b);
    if (cam->keys) cam->keys[arr_len(cam->keys) - 1].ease = EASE_IN;
    cam_key(cam, t + 1.3, orbit(pc, rr * 0.3f, deg, 1.1f), pc, EASE_LINEAR);
    Track *fade = shot_track(s, TRACK_FADE, -1, NULL, true);
    val_key(fade, t + 0.75, 0, EASE_SMOOTH);
    val_key(fade, t + 1.3, 1, EASE_HOLD);
    t += 1.3;
    val_key(fade, t + 0.05, 1, EASE_SMOOTH);
    val_key(fade, t + 0.7, 0, EASE_SMOOTH);
  } else if (last) {
    cam_key(cam, t + 3, orbit(c, dist * 0.97f, deg + 14, 1), c, EASE_SMOOTH);
    Track *fade = shot_track(s, TRACK_FADE, -1, NULL, true);
    t += 3;
    val_key(fade, t - 0.9, 0, EASE_SMOOTH);
    val_key(fade, t, 1, EASE_HOLD);
  }
  return t;
}

static void path_relative(char *out, size_t n, const char *from_file, const char *to) {
  // Both relative to the same place: walk up from from_file's folder.
  const char *a = from_file, *b = to, *last_a = from_file, *last_b = to;
  while (*a && *a == *b) {
    if (*a == '/') last_a = a + 1, last_b = b + 1;
    a++, b++;
  }
  out[0] = 0;
  for (const char *c = last_a; *c; c++)
    if (*c == '/') strncat(out, "../", n - strlen(out) - 1);
  strncat(out, last_b, n - strlen(out) - 1);
}

Shot *shot_auto(const char *path, const char *out_path, char *err, size_t errlen) {
  char *text = file_read(path, NULL);
  if (!text) {
    if (err) snprintf(err, errlen, "can't read %s", path);
    return NULL;
  }
  Arena ar = {0};
  Json *j = json_parse(&ar, text, err, errlen);
  free(text);
  if (!j) {
    arena_free(&ar);
    return NULL;
  }
  Shot *s = shot_new(json_str(j, "name", "Video"), out_path);
  Track *fade = shot_track(s, TRACK_FADE, -1, NULL, true);
  val_key(fade, 0, 1, EASE_SMOOTH);
  val_key(fade, 0.9, 0, EASE_SMOOTH);
  const Json *levels = json_get(j, "levels");
  double t = 0;
  char rel[512];
  if (levels && levels->type == J_ARR) {
    int i = 0;
    for (const Json *L = levels->head; L; L = L->next, i++) {
      const char *full = path_join(&ar, path, json_str(L, "file", ""));
      AssemblySpec *sp = spec_load(full, err, errlen);
      if (!sp) {
        shot_free(s);
        arena_free(&ar);
        return NULL;
      }
      Model *m = model_build(sp, (BuildOpts){0});
      path_relative(rel, sizeof rel, out_path, full);
      double t1 = auto_scene(s, i, m, json_str(L, "scale", NULL), json_str(L, "title", NULL), json_str(L, "text", NULL), json_str(L, "caption", NULL), t, i > 0,
                             L->next ? json_str(L, "into", NULL) : NULL, !L->next);
      arr_push(s->scenes, ((ShotScene){shot_str(s, rel), t, t1}));
      // A throttle moves through the machine's range while it is on screen.
      for (int c = 0; c < arr_len(sp->controls); c++) {
        Track *tr = shot_track(s, TRACK_CONTROL, i, sp->controls[c].id, true);
        double mx = is_set(sp->controls[c].max) ? sp->controls[c].max : 100;
        val_key(tr, t, mx * 0.3, EASE_SMOOTH);
        val_key(tr, t + (t1 - t) * 0.6, mx * 0.85, EASE_SMOOTH);
        val_key(tr, t1, mx * 0.5, EASE_SMOOTH);
      }
      t = t1;
      model_free(m);
      spec_free(sp);
    }
  } else {
    AssemblySpec *sp = spec_load(path, err, errlen);
    if (!sp) {
      shot_free(s);
      arena_free(&ar);
      return NULL;
    }
    Model *m = model_build(sp, (BuildOpts){0});
    path_relative(rel, sizeof rel, out_path, path);
    const char *info = arr_len(sp->layers) ? sp->layers[0].info : NULL;
    double t1 = auto_scene(s, 0, m, NULL, sp->name, info, NULL, 0, false, NULL, true);
    arr_push(s->scenes, ((ShotScene){shot_str(s, rel), 0, t1}));
    for (int c = 0; c < arr_len(sp->controls); c++) {
      Track *tr = shot_track(s, TRACK_CONTROL, 0, sp->controls[c].id, true);
      double mx = is_set(sp->controls[c].max) ? sp->controls[c].max : 100;
      val_key(tr, 0, mx * 0.3, EASE_SMOOTH);
      val_key(tr, t1 * 0.6, mx * 0.85, EASE_SMOOTH);
      val_key(tr, t1, mx * 0.5, EASE_SMOOTH);
    }
    model_free(m);
    spec_free(sp);
    t = t1;
  }
  s->duration = t;
  shot_recompute_duration(s);
  arena_free(&ar);
  return s;
}
