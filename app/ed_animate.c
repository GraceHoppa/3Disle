// The Animate tab: a video's timeline under its picture, and rendering it to a file.
#include "editor_int.h"
#include <unistd.h>

enum { WHAT_KEY, WHAT_CAPTION, WHAT_LABEL, WHAT_SCENE };

// ------------------------------------------------------------------ the document
static void anim_touch(Editor *e) {
  Anim *a = &e->a;
  hist_changed(&a->hist, a->sel);
  a->dirty = true;
}

static void release_player(ShotPlayer *p) {
  if (!p) return;
  for (int i = 0; i < arr_len(p->models); i++) r_release_model(p->models[i]);
  player_free(p);
}

void anim_release_models(Editor *e) {
  release_player(e->a.player);
  e->a.player = NULL;
}

void anim_free(Editor *e) {
  Anim *a = &e->a;
  if (a->video) video_end(a->video);
  release_player(a->video_player);
  release_player(a->player);
  shot_free(a->shot);
  hist_free(&a->hist);
  free(a->saved);
  memset(a, 0, sizeof *a);
}

static int docs_version(const Editor *e) {
  int v = arr_len(e->docs) * 100000;
  for (int i = 0; i < arr_len(e->docs); i++) v += e->docs[i]->version;
  return v;
}

static void rebuild_player(Editor *e) {
  Anim *a = &e->a;
  SpecOverride *over = NULL;
  for (int i = 0; i < arr_len(e->docs); i++)
    if (e->docs[i]->path[0]) arr_push(over, ((SpecOverride){e->docs[i]->path, e->docs[i]->spec}));
  char err[1024] = "";
  ShotPlayer *p = player_new_over(a->shot, (BuildOpts){.paint = true}, over, arr_len(over), err, sizeof err);
  arr_free(over);
  if (!p) {
    ed_status(e, true, "%s", err);
  } else {
    release_player(a->player);
    a->player = p;
  }
  a->stale = false;
  a->doc_version = docs_version(e);
  ed_sweep_textures(e);
}

static void fit_timeline(Editor *e) {
  Anim *a = &e->a;
  a->px_per_s = 0; // worked out when next drawn, when the width is known
  a->left = 0;
}

static void adopt_shot(Editor *e, Shot *s) {
  Anim *a = &e->a;
  shot_free(a->shot);
  a->shot = s;
  char *text = shot_write(s);
  hist_init(&a->hist, text);
  free(a->saved);
  a->saved = text;
  a->dirty = false;
  a->stale = true;
  a->t = 0;
  a->playing = false;
  a->detached = false;
  a->sel_on = false;
  a->row_scroll = 0;
  stage_init(&a->stage);
  fit_timeline(e);
}

bool anim_open(Editor *e, const char *path, char *err, size_t errlen) {
  Shot *s = shot_load(path, err, errlen);
  if (!s) return false;
  adopt_shot(e, s);
  return true;
}

bool anim_save(Editor *e) {
  Anim *a = &e->a;
  if (!a->shot) return false;
  if (!a->shot->path[0]) {
    e->modal_for_video = true;
    ed_modal_files(e, MODAL_SAVE_AS);
    return false;
  }
  if (!shot_save(a->shot, a->shot->path)) {
    ed_status(e, true, "Couldn't write %s", a->shot->path);
    return false;
  }
  free(a->saved);
  a->saved = shot_write(a->shot);
  a->dirty = false;
  ed_status(e, false, "Saved %s", a->shot->path);
  return true;
}

void anim_settle(Editor *e) {
  Anim *a = &e->a;
  if (!a->hist.open || e->in->down[0] || ui_wants_keyboard()) return;
  char *text = shot_write(a->shot);
  hist_settle(&a->hist, text);
  a->dirty = !a->saved || strcmp(text, a->saved) != 0;
  free(text);
}

static bool same_scenes(const Shot *x, const Shot *y) {
  if (arr_len(x->scenes) != arr_len(y->scenes)) return false;
  for (int i = 0; i < arr_len(x->scenes); i++)
    if (strcmp(x->scenes[i].file, y->scenes[i].file)) return false;
  return true;
}

void anim_undo(Editor *e, bool redo) {
  Anim *a = &e->a;
  if (a->hist.open) {
    char *text = shot_write(a->shot);
    hist_settle(&a->hist, text);
    free(text);
  }
  Sel sel;
  const char *text = redo ? hist_redo(&a->hist, &sel, a->sel) : hist_undo(&a->hist, &sel, a->sel);
  if (!text) {
    ed_status(e, false, "Nothing to %s", redo ? "redo" : "undo");
    return;
  }
  char err[256] = "";
  Shot *s = shot_parse(text, a->shot->path, err, sizeof err);
  if (!s) return;
  if (a->player && same_scenes(s, a->shot)) {
    a->player->shot = s;
    player_invalidate(a->player);
  } else {
    a->stale = true;
    if (a->player) a->player->shot = s;
  }
  shot_free(a->shot);
  a->shot = s;
  a->sel = sel;
  a->sel_on = false;
  a->dirty = !a->saved || strcmp(text, a->saved) != 0;
  ed_status(e, false, redo ? "Redone" : "Undone");
}

// A name nothing else uses, beside the device.
static void free_shot_path(const char *device, char *out, size_t n) {
  char base[1024];
  snprintf(base, sizeof base, "%s", device);
  char *dot = strrchr(base, '.');
  if (dot && !strchr(dot, '/')) *dot = 0;
  snprintf(out, n, "%s.shot.json", base);
  for (int i = 2; !access(out, F_OK) && i < 1000; i++) snprintf(out, n, "%s-%d.shot.json", base, i);
}

static void new_auto(Editor *e) {
  Doc *d = ed_doc(e);
  if (!d->path[0]) {
    ed_status(e, true, "Save the device first: a video names its file");
    ed_doc_save(e, d);
    return;
  }
  if (d->dirty && !ed_doc_save(e, d)) return;
  char path[1100], err[1024] = "";
  free_shot_path(d->path, path, sizeof path);
  Shot *s = shot_auto(d->path, path, err, sizeof err);
  if (!s) {
    ed_status(e, true, "%s", err);
    return;
  }
  adopt_shot(e, s);
  e->a.dirty = true;
  ed_status(e, false, "Wrote a video from what the device says: %.1f s. Not saved yet.", s->duration);
}

static void new_empty(Editor *e) {
  Doc *d = ed_doc(e);
  if (!d->path[0]) {
    ed_status(e, true, "Save the device first: a video names its file");
    ed_doc_save(e, d);
    return;
  }
  char path[1100], rel[1100];
  free_shot_path(d->path, path, sizeof path);
  Shot *s = shot_new(d->spec->name, path);
  s->fps = 30, s->w = 1920, s->h = 1080, s->duration = 10;
  path_rel(path, d->path, rel, sizeof rel);
  arr_push(s->scenes, ((ShotScene){shot_str(s, rel), 0, 10}));
  adopt_shot(e, s);
  e->a.dirty = true;
}

// ------------------------------------------------------------------ keys
static float scene_aspect(const Shot *s) { return (float)s->w / (float)maxi(s->h, 1); }

static void current_camera(Editor *e, Vec3 *pos, Vec3 *target, float *fov) {
  Anim *a = &e->a;
  if (a->detached) {
    *pos = a->stage.cam.pos, *target = a->stage.cam.target, *fov = a->stage.cam.fov_deg;
  } else {
    *pos = a->frame.cam_pos, *target = a->frame.cam_target, *fov = a->frame.fov > 0 ? a->frame.fov : 40;
  }
}

static double snap_t(const Shot *s, double t) {
  double f = fmax(s->fps, 1);
  return fmax(round(t * f) / f, 0);
}

static int track_index(const Shot *s, const Track *t) { return (int)(t - s->tracks); }

static void select_key(Anim *a, int track, int key) {
  a->sel_on = true;
  a->sel_what = WHAT_KEY;
  a->sel = (Sel){SEL_DEVICE, track, key};
}

static void set_camera_key(Editor *e) {
  Anim *a = &e->a;
  Shot *s = a->shot;
  int scene = shot_scene_at(s, a->t);
  if (scene < 0) return;
  Vec3 pos, target;
  float fov;
  current_camera(e, &pos, &target, &fov);
  Track *tr = shot_track(s, TRACK_CAMERA, scene, NULL, true);
  Key k = {.t = snap_t(s, a->t), .fov = fov, .ease = EASE_SMOOTH};
  k.v[0] = pos.x, k.v[1] = pos.y, k.v[2] = pos.z;
  k.target[0] = target.x, k.target[1] = target.y, k.target[2] = target.z;
  int i = track_set_key(tr, k);
  select_key(a, track_index(s, tr), i);
  a->detached = false;
  anim_touch(e);
  shot_recompute_duration(s);
  ed_status(e, false, "Camera key at %.2f s", k.t);
}

// A key where the playhead is, holding whatever the track reads there, so adding one changes nothing.
static void add_key_at(Editor *e, int track, double t) {
  Anim *a = &e->a;
  Shot *s = a->shot;
  Track *tr = &s->tracks[track];
  if (tr->kind == TRACK_CAMERA) {
    double keep = a->t;
    a->t = t;
    set_camera_key(e);
    a->t = keep;
    return;
  }
  Key k = {.t = snap_t(s, t), .fov = 40, .ease = EASE_SMOOTH};
  if (tr->kind == TRACK_OFFSET) track_vec3(tr, t, k.v);
  else k.v[0] = track_value(tr, t, tr->kind == TRACK_OPACITY || tr->kind == TRACK_FLOW ? 1 : 0);
  int i = track_set_key(tr, k);
  select_key(a, track, i);
  anim_touch(e);
  shot_recompute_duration(s);
}

static void delete_selected(Editor *e) {
  Anim *a = &e->a;
  Shot *s = a->shot;
  if (!a->sel_on) return;
  switch (a->sel_what) {
  case WHAT_KEY: {
    Track *tr = &s->tracks[a->sel.layer];
    if (a->sel.index >= 0 && a->sel.index < arr_len(tr->keys)) arr_remove(tr->keys, a->sel.index);
    if (!arr_len(tr->keys)) {
      arr_free(tr->keys);
      arr_remove(s->tracks, a->sel.layer);
    }
    break;
  }
  case WHAT_CAPTION: arr_remove(s->captions, a->sel.index); break;
  case WHAT_LABEL: arr_remove(s->labels, a->sel.index); break;
  case WHAT_SCENE:
    if (arr_len(s->scenes) <= 1) {
      ed_status(e, true, "A video needs at least one scene");
      return;
    }
    {
      int gone = a->sel.index;
      for (int i = arr_len(s->tracks) - 1; i >= 0; i--) {
        if (s->tracks[i].scene == gone) {
          arr_free(s->tracks[i].keys);
          arr_remove(s->tracks, i);
        } else if (s->tracks[i].scene > gone) {
          s->tracks[i].scene--;
        }
      }
      for (int i = arr_len(s->labels) - 1; i >= 0; i--) {
        if (s->labels[i].scene == gone) arr_remove(s->labels, i);
        else if (s->labels[i].scene > gone) s->labels[i].scene--;
      }
      arr_remove(s->scenes, gone);
      a->stale = true;
    }
    break;
  }
  a->sel_on = false;
  anim_touch(e);
}

void anim_keys(Editor *e) {
  Anim *a = &e->a;
  const UiInput *in = e->in;
  if (!a->shot || ui_wants_keyboard() || e->modal != MODAL_NONE) return;
  if (in->keys[KEY_SPACE]) {
    a->playing = !a->playing;
    if (a->playing && a->t >= a->shot->duration - 1e-3) a->t = 0;
    a->detached = false;
  }
  if (in->keys[KEY_K]) {
    if (a->sel_on && a->sel_what == WHAT_KEY && a->shot->tracks[a->sel.layer].kind != TRACK_CAMERA) add_key_at(e, a->sel.layer, a->t);
    else set_camera_key(e);
  }
  if (in->keys[KEY_DELETE]) delete_selected(e);
  double frame = 1.0 / fmax(a->shot->fps, 1);
  if (in->keys[KEY_LEFT]) a->t = fmax(a->t - frame, 0), a->playing = false, a->detached = false;
  if (in->keys[KEY_RIGHT]) a->t = fmin(a->t + frame, a->shot->duration), a->playing = false, a->detached = false;
  if (in->keys[KEY_HOME]) a->t = 0, a->detached = false;
  if (in->keys[KEY_END]) a->t = a->shot->duration, a->detached = false;
}

// ------------------------------------------------------------------ the timeline
static const u32 TRACK_COL[TRACK_KIND_COUNT] = {
    RGBA(0x9c, 0xff, 0x3a, 255), RGBA(0x5f, 0xe0, 0xff, 255), RGBA(0x9a, 0xa5, 0x9c, 255), RGBA(0xff, 0xae, 0x42, 255),
    RGBA(0xff, 0x7a, 0x2f, 255), RGBA(0xc4, 0x8c, 0xff, 255), RGBA(0x8a, 0xb4, 0xff, 255), RGBA(0xff, 0xe0, 0x5a, 255),
};

static void diamond(float cx, float cy, float r, u32 c) {
  ui_line(cx - r, cy, cx + r, cy, r * 1.15f, c);
  ui_line(cx - r * 0.55f, cy, cx + r * 0.55f, cy, r * 1.9f, c);
}

static void format_time(char *out, size_t n, double t) {
  int m = (int)(t / 60);
  snprintf(out, n, "%02d:%06.3f", m, t - m * 60);
}

static double nice_step(double seconds) {
  static const double S_[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300};
  for (int i = 0; i < COUNT(S_); i++)
    if (S_[i] >= seconds) return S_[i];
  return 600;
}

static void ruler(Editor *e, Rect r, float tx, float tw) {
  Anim *a = &e->a;
  const UiInput *in = e->in;
  ui_rect(r, RGBA(9, 15, 11, 255));
  ui_mark("ruler", rect(tx, r.y, tw, r.h));
  double step = nice_step((double)S(70) / (double)a->px_per_s);
  double first = floor(a->left / step) * step;
  for (double t = first; ; t += step) {
    float x = tx + (float)(t - a->left) * a->px_per_s;
    if (x > tx + tw) break;
    if (x < tx) continue;
    ui_rect(rect(x, r.y + r.h - S(6), maxf(1, S(1)), S(6)), C_LINE);
    char lbl[32];
    if (step >= 1) snprintf(lbl, sizeof lbl, "%d:%02d", (int)(t / 60), (int)fmod(t, 60));
    else snprintf(lbl, sizeof lbl, "%.1f", t);
    ui_text(x + S(3), r.y + S(3), S(10), FONT_REGULAR, C_FAINT, lbl);
  }
  if (ui_hovering(rect(tx, r.y, tw, r.h)) && in->pressed[0] && !a->drag) a->drag = 5;
}

static float blockx(const Anim *a, float tx, double t) { return tx + (float)(t - a->left) * a->px_per_s; }

// A bar on a strip: what you can drag by the body or by either end.
static void strip_block(Editor *e, Rect strip, float tx, float tw, int what, int index, double t0, double t1, const char *text, u32 col, const char *mark) {
  Anim *a = &e->a;
  const UiInput *in = e->in;
  float x0 = maxf(blockx(a, tx, t0), tx), x1 = minf(blockx(a, tx, t1), tx + tw);
  if (x1 <= x0) return;
  Rect b = rect(x0, strip.y + S(2), x1 - x0, strip.h - S(4));
  bool on = a->sel_on && a->sel_what == what && a->sel.index == index;
  ui_rect(b, c_alpha(col, on ? 0.55f : 0.22f));
  ui_border(b, on ? C_BRIGHT : c_alpha(col, 0.5f), maxf(1, S(1)));
  ui_clip_push(b);
  ui_text(b.x + S(4), b.y + S(1), S(10), FONT_REGULAR, C_TEXT, text);
  ui_clip_pop();
  char id[64];
  snprintf(id, sizeof id, "%s:%d", mark, index);
  ui_mark(id, b);
  if (ui_hovering(b) && in->pressed[0] && !a->drag) {
    a->sel_on = true;
    a->sel_what = what;
    a->sel = (Sel){SEL_DEVICE, -1, index};
    float edge = S(5);
    a->drag = what == WHAT_SCENE ? 0 : in->mx < b.x + edge ? 3 : in->mx > b.x + b.w - edge ? 4 : 2;
    a->drag_a = t0, a->drag_b = t1;
    a->drag_mx = in->mx;
    if (what == WHAT_SCENE && in->mx > b.x + b.w - edge && index + 1 < arr_len(a->shot->scenes)) {
      a->drag = 6;
      a->drag_t0 = t1;
    }
    ui_unfocus();
  }
}

static void track_name(const Track *t, char *out, size_t n) {
  if (t->name) snprintf(out, n, "%s \xC2\xB7 %s", TRACK_NAMES[t->kind], t->name);
  else snprintf(out, n, "%s", TRACK_NAMES[t->kind]);
}

static void timeline_drag(Editor *e) {
  Anim *a = &e->a;
  Shot *s = a->shot;
  const UiInput *in = e->in;
  if (!a->drag) return;
  if (!in->down[0]) {
    a->drag = 0;
    return;
  }
  double dt = (double)((in->mx - a->drag_mx) / a->px_per_s);
  switch (a->drag) {
  case 1: {
    Track *tr = &s->tracks[a->sel.layer];
    double nt = snap_t(s, a->drag_t0 + dt);
    int i = a->sel.index;
    if (i < 0 || i >= arr_len(tr->keys) || fabs(tr->keys[i].t - nt) < 1e-9) break;
    tr->keys[i].t = nt;
    // The array stays in time order; the key being dragged keeps its place in the selection.
    while (i > 0 && tr->keys[i - 1].t > nt) {
      Key k = tr->keys[i];
      tr->keys[i] = tr->keys[i - 1];
      tr->keys[i - 1] = k;
      i--;
    }
    while (i + 1 < arr_len(tr->keys) && tr->keys[i + 1].t < nt) {
      Key k = tr->keys[i];
      tr->keys[i] = tr->keys[i + 1];
      tr->keys[i + 1] = k;
      i++;
    }
    a->sel.index = i;
    shot_recompute_duration(s);
    anim_touch(e);
    break;
  }
  case 2:
  case 3:
  case 4: {
    double t0 = a->drag_a, t1 = a->drag_b;
    if (a->drag != 4) t0 = snap_t(s, a->drag_a + dt);
    if (a->drag != 3) t1 = snap_t(s, a->drag_b + dt);
    if (a->drag == 2 && t0 == 0) t1 = a->drag_b - a->drag_a;
    if (t1 < t0 + 0.05) {
      if (a->drag == 3) t0 = t1 - 0.05;
      else t1 = t0 + 0.05;
    }
    if (a->sel_what == WHAT_CAPTION) s->captions[a->sel.index].t0 = t0, s->captions[a->sel.index].t1 = t1;
    if (a->sel_what == WHAT_LABEL) s->labels[a->sel.index].t0 = t0, s->labels[a->sel.index].t1 = t1;
    anim_touch(e);
    break;
  }
  case 5: break; // scrubbing is handled with the playhead
  case 6: {
    int i = a->sel.index;
    double nt = snap_t(s, a->drag_t0 + dt);
    nt = fmax(nt, s->scenes[i].start + 0.1);
    nt = fmin(nt, s->scenes[i + 1].end - 0.1);
    s->scenes[i].end = nt;
    s->scenes[i + 1].start = nt;
    player_invalidate(a->player);
    anim_touch(e);
    break;
  }
  default: break;
  }
}

static void timeline(Editor *e, Rect r) {
  Anim *a = &e->a;
  Shot *s = a->shot;
  const UiInput *in = e->in;
  ui_rect(r, RGBA(7, 12, 9, 255));
  ui_rect(rect(r.x, r.y, r.w, maxf(1, S(1))), C_LINE);
  ui_block(r);
  float bar_h = S(30), lw = S(170), rh = S(20);
  // Tools above the rows.
  float x = r.x + S(8), by = r.y + S(4), bh = S(22);
  if (ui_button(rect(x, by, S(58), bh), a->playing ? "PAUSE" : "PLAY", a->playing ? BTN_ON : 0)) {
    a->playing = !a->playing;
    if (a->playing && a->t >= s->duration - 1e-3) a->t = 0;
    a->detached = false;
  }
  x += S(62);
  if (ui_button(rect(x, by, S(46), bh), "START", 0)) a->t = 0, a->detached = false;
  x += S(56);
  char now[32], tot[32], tm[80];
  format_time(now, sizeof now, a->t);
  format_time(tot, sizeof tot, s->duration);
  snprintf(tm, sizeof tm, "%s / %s", now, tot);
  ui_text(x, by + S(3), S(12), FONT_REGULAR, C_BRIGHT, tm);
  x += ui_text_width(S(12), FONT_REGULAR, 0, tm) + S(14);
  if (ui_button(rect(x, by, S(112), bh), a->detached ? "SET CAMERA HERE" : "CAMERA KEY", a->detached ? BTN_ON : 0)) set_camera_key(e);
  x += S(116);
  if (ui_button(rect(x, by, S(40), bh), "FIT", 0)) fit_timeline(e);
  x += S(44);
  if (ui_button(rect(x, by, S(46), bh), "DEL##key", a->sel_on ? 0 : BTN_DISABLED)) delete_selected(e);
  x += S(50);
  if (a->detached) ui_text(x + S(6), by + S(4), S(11), FONT_REGULAR, C_DIM, "Free view: drag the ruler to go back to the camera");

  float tx = r.x + lw, tw = r.w - lw - S(4);
  if (a->px_per_s <= 0) a->px_per_s = tw / (float)fmax(s->duration * 1.02, 0.5);

  // Wheel: zoom about the pointer. Shift: slide. Over the names: scroll the rows.
  Rect rows_all = rect(r.x, r.y + bar_h, r.w, r.h - bar_h);
  if (ui_hovering(rows_all) && in->wheel != 0) {
    if (in->mx < tx) {
      a->row_scroll = maxf(0, a->row_scroll - in->wheel * S(30));
    } else if (in->shift) {
      a->left = fmax(a->left - (double)in->wheel * (double)(S(60) / a->px_per_s), 0);
    } else {
      double at = a->left + (double)((in->mx - tx) / a->px_per_s);
      float before = a->px_per_s;
      a->px_per_s = clampf(a->px_per_s * powf(1.2f, in->wheel), 2, 4000);
      (void)before;
      a->left = fmax(at - (double)((in->mx - tx) / a->px_per_s), 0);
    }
  }

  float y = r.y + bar_h;
  ruler(e, rect(r.x, y, r.w, S(20)), tx, tw);
  y += S(20);
  ui_clip_push(rect(tx, y, tw, r.y + r.h - y));

  // Strips: scenes, captions, labels.
  static const char *const STRIP[] = {"scenes", "captions", "labels"};
  float strip_y = y;
  for (int i = 0; i < arr_len(s->scenes); i++) {
    const char *f = strrchr(s->scenes[i].file, '/');
    strip_block(e, rect(tx, strip_y, tw, rh), tx, tw, WHAT_SCENE, i, s->scenes[i].start, s->scenes[i].end, f ? f + 1 : s->scenes[i].file, C_ACCENT, "scene");
  }
  strip_y += rh;
  for (int i = 0; i < arr_len(s->captions); i++) {
    const ShotCaption *c = &s->captions[i];
    strip_block(e, rect(tx, strip_y, tw, rh), tx, tw, WHAT_CAPTION, i, c->t0, c->t1, c->title ? c->title : (c->caption ? c->caption : "caption"), RGBA(0x5f, 0xe0, 0xff, 255), "caption");
  }
  strip_y += rh;
  int cur_scene = shot_scene_at(s, a->t);
  // Labels that overlap in time stack in lanes, so each can still be grabbed.
  double lane_end[6];
  for (int i = 0; i < 6; i++) lane_end[i] = -1;
  int lanes = 1;
  for (int i = 0; i < arr_len(s->labels); i++) {
    const ShotLabel *l = &s->labels[i];
    if (l->scene != cur_scene) continue;
    int at = 5;
    for (int k = 0; k < 6; k++)
      if (lane_end[k] <= l->t0) {
        at = k;
        break;
      }
    lane_end[at] = l->t1;
    lanes = maxi(lanes, at + 1);
    strip_block(e, rect(tx, strip_y + (float)at * rh, tw, rh), tx, tw, WHAT_LABEL, i, l->t0, l->t1, l->text ? l->text : l->part, RGBA(0xff, 0xe0, 0x5a, 255), "label");
  }
  strip_y += rh * (float)lanes;
  ui_clip_pop();
  for (int i = 0; i < 3; i++) ui_text(r.x + S(10), y + (float)i * rh + S(3), S(10.5f), FONT_REGULAR, C_FAINT, STRIP[i]);
  y = strip_y;
  ui_rect(rect(r.x, y, r.w, maxf(1, S(1))), C_LINE);

  // Track rows: this scene's, and those that span the whole video.
  Rect rows = rect(r.x, y, r.w, r.y + r.h - y);
  ui_clip_push(rows);
  int shown = 0;
  float ry = y - a->row_scroll;
  bool pressed = in->pressed[0] && !a->drag;
  for (int i = 0; i < arr_len(s->tracks); i++) {
    Track *t = &s->tracks[i];
    if (t->scene != -1 && t->scene != cur_scene) continue;
    shown++;
    Rect row_r = rect(r.x, ry, r.w, rh);
    bool sel_row = a->sel_on && a->sel_what == WHAT_KEY && a->sel.layer == i;
    if (sel_row) ui_rect(row_r, RGBA(14, 24, 12, 255));
    ui_rect(rect(r.x, ry + rh - maxf(1, S(1)), r.w, maxf(1, S(1))), RGBA(14, 22, 16, 255));
    char name[200], mark[64];
    track_name(t, name, sizeof name);
    ui_rect(rect(r.x + S(8), ry + S(6), S(3), S(8)), TRACK_COL[t->kind]);
    ui_text_fit(r.x + S(16), ry + S(3), lw - S(22), S(10.5f), FONT_REGULAR, sel_row ? C_TEXT : C_DIM, name);
    snprintf(mark, sizeof mark, "row:%d", i);
    ui_mark(mark, rect(tx, ry, tw, rh));
    // Keys.
    Rect tr_r = rect(tx, ry, tw, rh);
    ui_clip_push(rect(tx, maxf(ry, y), tw, minf(rh, r.y + r.h - ry)));
    int hit = -1;
    float best = S(7);
    for (int k = 0; k < arr_len(t->keys); k++) {
      float kx = blockx(a, tx, t->keys[k].t);
      if (kx < tx - S(8) || kx > tx + tw + S(8)) continue;
      bool on = sel_row && a->sel.index == k;
      diamond(kx, ry + rh / 2, on ? S(5.5f) : S(4.5f), on ? C_BRIGHT : TRACK_COL[t->kind]);
      if (k + 1 < arr_len(t->keys) && t->keys[k].ease != EASE_HOLD) {
        float nx = blockx(a, tx, t->keys[k + 1].t);
        ui_rect(rect(kx, ry + rh / 2 - maxf(0.5f, S(0.5f)), nx - kx, maxf(1, S(1))), c_alpha(TRACK_COL[t->kind], 0.3f));
      }
      snprintf(mark, sizeof mark, "key:%d:%d", i, k);
      ui_mark(mark, rect(kx - S(5), ry + S(2), S(10), rh - S(4)));
      float dist = fabsf(in->mx - kx);
      if (dist < best && ui_hovering(tr_r)) best = dist, hit = k;
    }
    ui_clip_pop();
    if (pressed && ui_hovering(tr_r)) {
      if (hit >= 0) {
        select_key(a, i, hit);
        a->drag = 1;
        a->drag_t0 = t->keys[hit].t;
        a->drag_mx = in->mx;
        ui_unfocus();
      } else if (in->double_click) {
        double at = a->left + (double)((in->mx - tx) / a->px_per_s);
        add_key_at(e, i, at);
        s = a->shot;
      } else {
        a->sel_on = false;
      }
    }
    ry += rh;
  }
  ui_clip_pop();
  float content = (float)shown * rh, room = r.y + r.h - y;
  a->row_scroll = minf(a->row_scroll, maxf(content - room, 0));
  if (!shown) ui_text(tx + S(10), y + S(8), S(11), FONT_REGULAR, C_FAINT, "No tracks in this scene yet: orbit the view and press K, or add one on the right.");

  timeline_drag(e);

  // The playhead sits on top of everything.
  if (a->drag == 5) {
    a->t = fmin(snap_t(s, a->left + (double)((in->mx - tx) / a->px_per_s)), s->duration);
    a->playing = false;
    a->detached = false;
    if (!in->down[0]) a->drag = 0;
  }
  float px = blockx(a, tx, a->t);
  if (px >= tx && px <= tx + tw) {
    ui_rect(rect(px - maxf(0.5f, S(0.5f)), r.y + bar_h, maxf(1, S(1.2f)), r.h - bar_h), C_BRIGHT);
    ui_rect(rect(px - S(4), r.y + bar_h, S(8), S(7)), C_BRIGHT);
  }
}

// ------------------------------------------------------------------ the panel
typedef struct {
  Editor *e;
  float x, y, w, lw, rh;
} AF;

static Rect afrow(AF *f, const char *label) {
  ui_text_fit(f->x, f->y + (f->rh - ui_line_height(S(11))) / 2, f->lw - S(6), S(11), FONT_REGULAR, C_DIM, label);
  Rect r = rect(f->x + f->lw, f->y, f->w - f->lw, f->rh);
  f->y += f->rh + S(3);
  return r;
}

static void afsection(AF *f, const char *title) {
  f->y += S(8);
  ui_text_sp(f->x, f->y, S(10), FONT_BOLD, C_ACCENT, S(1.6f), title);
  ui_rect(rect(f->x, f->y + S(15), f->w, maxf(1, S(1))), C_LINE);
  f->y += S(21);
}

static void afnum(AF *f, const char *label, double *v, double step, const char *unit) {
  if (ui_drag(afrow(f, label), label, v, step, unit)) anim_touch(f->e);
}

static void aftext(AF *f, const char *label, const char **field) {
  Shot *s = f->e->a.shot;
  char buf[1500];
  snprintf(buf, sizeof buf, "%s", *field ? *field : "");
  if (ui_text_field(afrow(f, label), label, buf, sizeof buf, NULL)) {
    *field = buf[0] ? shot_str(s, buf) : NULL;
    anim_touch(f->e);
  }
}

static void afnums3(AF *f, const char *label, double *v, double step) {
  Rect r = afrow(f, label);
  float w = (r.w - 2 * S(3)) / 3;
  static const char *const AX[] = {"x", "y", "z"};
  char id[64];
  for (int i = 0; i < 3; i++) {
    snprintf(id, sizeof id, "%s %s", label, AX[i]);
    if (ui_drag(rect(r.x + (float)i * (w + S(3)), r.y, w, r.h), id, &v[i], step, NULL)) anim_touch(f->e);
  }
}

static void panel_key(AF *f) {
  Anim *a = &f->e->a;
  Shot *s = a->shot;
  Track *tr = &s->tracks[a->sel.layer];
  if (a->sel.index < 0 || a->sel.index >= arr_len(tr->keys)) return;
  Key *k = &tr->keys[a->sel.index];
  char t[200];
  track_name(tr, t, sizeof t);
  afsection(f, "KEY");
  ui_text_fit(f->x, f->y, f->w, S(11), FONT_REGULAR, C_TEXT, t);
  f->y += S(18);
  double when = k->t;
  if (ui_drag(afrow(f, "Time"), "Key time", &when, 1.0 / fmax(s->fps, 1), "s")) {
    k->t = fmax(when, 0);
    int i = a->sel.index;
    while (i > 0 && tr->keys[i - 1].t > tr->keys[i].t) {
      Key x = tr->keys[i];
      tr->keys[i] = tr->keys[i - 1];
      tr->keys[i - 1] = x;
      i--;
    }
    while (i + 1 < arr_len(tr->keys) && tr->keys[i + 1].t < tr->keys[i].t) {
      Key x = tr->keys[i];
      tr->keys[i] = tr->keys[i + 1];
      tr->keys[i + 1] = x;
      i++;
    }
    a->sel.index = i;
    k = &tr->keys[i];
    shot_recompute_duration(s);
    anim_touch(f->e);
  }
  int ease = (int)k->ease;
  if (ui_dropdown(afrow(f, "Then"), "Ease", &ease, EASE_NAMES, EASE_COUNT)) {
    k->ease = (Ease)ease;
    anim_touch(f->e);
  }
  switch (tr->kind) {
  case TRACK_CAMERA:
    afnums3(f, "Position", k->v, 0.5);
    afnums3(f, "Looks at", k->target, 0.5);
    afnum(f, "Field of view", &k->fov, 1, "\xC2\xB0");
    if (ui_button(rect(f->x, f->y, f->w, f->rh), "SET FROM THE VIEW", 0)) {
      Vec3 pos, target;
      float fov;
      current_camera(f->e, &pos, &target, &fov);
      k->v[0] = pos.x, k->v[1] = pos.y, k->v[2] = pos.z;
      k->target[0] = target.x, k->target[1] = target.y, k->target[2] = target.z;
      k->fov = fov;
      a->detached = false;
      anim_touch(f->e);
    }
    f->y += f->rh + S(3);
    break;
  case TRACK_OFFSET: afnums3(f, "Moved by", k->v, 0.5); break;
  case TRACK_EXPLODE: afnum(f, "Apart", &k->v[0], 0.01, NULL); break;
  case TRACK_FADE: afnum(f, "Black", &k->v[0], 0.01, NULL); break;
  case TRACK_FLOW: afnum(f, "Speed", &k->v[0], 0.05, NULL); break;
  case TRACK_OPACITY: afnum(f, "Solid", &k->v[0], 0.01, NULL); break;
  case TRACK_GLOW: afnum(f, "Glow", &k->v[0], 0.01, NULL); break;
  case TRACK_CONTROL: afnum(f, "Value", &k->v[0], 1, NULL); break;
  default: break;
  }
}

static void panel_caption(AF *f) {
  Anim *a = &f->e->a;
  ShotCaption *c = &a->shot->captions[a->sel.index];
  afsection(f, "CAPTION");
  afnum(f, "From", &c->t0, 0.05, "s");
  afnum(f, "To", &c->t1, 0.05, "s");
  aftext(f, "Scale", &c->scale);
  aftext(f, "Title", &c->title);
  aftext(f, "Text", &c->text);
  aftext(f, "Caption", &c->caption);
}

static void panel_label(AF *f) {
  Anim *a = &f->e->a;
  ShotLabel *l = &a->shot->labels[a->sel.index];
  afsection(f, "LABEL");
  afnum(f, "From", &l->t0, 0.05, "s");
  afnum(f, "To", &l->t1, 0.05, "s");
  aftext(f, "On part", &l->part);
  aftext(f, "Says", &l->text);
}

static void panel_scene(AF *f) {
  Anim *a = &f->e->a;
  ShotScene *c = &a->shot->scenes[a->sel.index];
  afsection(f, "SCENE");
  afnum(f, "Starts", &c->start, 0.05, "s");
  afnum(f, "Ends", &c->end, 0.05, "s");
  const char *was = c->file;
  aftext(f, "File", &c->file);
  if (was != c->file) {
    if (!c->file) c->file = was;
    a->stale = true;
  }
}

static Track *new_track(Editor *e, TrackKind kind, int scene, const char *name, double value) {
  Anim *a = &e->a;
  Shot *s = a->shot;
  Track *tr = shot_track(s, kind, scene, name, true);
  Key k = {.t = snap_t(s, a->t), .fov = 40, .ease = EASE_SMOOTH};
  k.v[0] = value;
  int i = track_set_key(tr, k);
  select_key(a, track_index(s, tr), i);
  anim_touch(e);
  return tr;
}

static void panel_add(AF *f) {
  Editor *e = f->e;
  Anim *a = &e->a;
  Shot *s = a->shot;
  int scene = shot_scene_at(s, a->t);
  float gap = S(3), half = (f->w - gap) / 2, third = (f->w - 2 * gap) / 3;
  afsection(f, "ADD AT THE PLAYHEAD");
  if (ui_button(rect(f->x, f->y, third, f->rh), "APART", 0)) {
    Track *t0 = shot_track(s, TRACK_EXPLODE, scene, NULL, false);
    new_track(e, TRACK_EXPLODE, scene, NULL, track_value(t0, a->t, 0));
  }
  if (ui_button(rect(f->x + third + gap, f->y, third, f->rh), "FADE", 0)) {
    Track *t0 = shot_track(s, TRACK_FADE, -1, NULL, false);
    new_track(e, TRACK_FADE, -1, NULL, track_value(t0, a->t, 0));
  }
  if (ui_button(rect(f->x + 2 * (third + gap), f->y, third, f->rh), "FLOW", 0)) {
    Track *t0 = shot_track(s, TRACK_FLOW, -1, NULL, false);
    new_track(e, TRACK_FLOW, -1, NULL, track_value(t0, a->t, 1));
  }
  f->y += f->rh + gap;
  if (ui_button(rect(f->x, f->y, half, f->rh), "CAPTION", 0)) {
    arr_push(s->captions, ((ShotCaption){a->t, fmin(a->t + 4, fmax(s->duration, a->t + 1)), NULL, shot_str(s, "Title"), NULL, NULL}));
    a->sel_on = true, a->sel_what = WHAT_CAPTION, a->sel = (Sel){SEL_DEVICE, -1, arr_len(s->captions) - 1};
    anim_touch(e);
  }
  ShotFrame *fr = &a->frame;
  if (fr->model && arr_len(fr->model->spec->controls)) {
    const ControlSpec *c = &fr->model->spec->controls[0];
    if (ui_button(rect(f->x + half + gap, f->y, half, f->rh), "CONTROL", 0)) new_track(e, TRACK_CONTROL, scene, c->id, model_get_value(fr->model, c->id));
  }
  f->y += f->rh + gap;
  afsection(f, "THE PART YOU LAST CLICKED");
  bool has = e->fbuf_part[0] != 0;
  ui_text_fit(f->x, f->y, f->w, S(11.5f), FONT_REGULAR, has ? C_TEXT : C_FAINT, has ? e->fbuf_part : "Click a part in the picture");
  f->y += S(18);
  int fl = has ? 0 : BTN_DISABLED;
  if (ui_button(rect(f->x, f->y, half, f->rh), "MOVE IT", fl)) {
    Track *t0 = shot_track(s, TRACK_OFFSET, scene, e->fbuf_part, false);
    double v[3] = {0, 0, 0};
    if (t0) track_vec3(t0, a->t, v);
    Track *tr = new_track(e, TRACK_OFFSET, scene, e->fbuf_part, 0);
    Key *k = &tr->keys[a->sel.index];
    k->v[0] = v[0], k->v[1] = v[1], k->v[2] = v[2];
  }
  if (ui_button(rect(f->x + half + gap, f->y, half, f->rh), "FADE IT", fl)) {
    Track *t0 = shot_track(s, TRACK_OPACITY, scene, e->fbuf_part, false);
    new_track(e, TRACK_OPACITY, scene, e->fbuf_part, track_value(t0, a->t, 1));
  }
  f->y += f->rh + gap;
  if (ui_button(rect(f->x, f->y, half, f->rh), "LIGHT IT", fl)) {
    Track *t0 = shot_track(s, TRACK_GLOW, scene, e->fbuf_part, false);
    new_track(e, TRACK_GLOW, scene, e->fbuf_part, track_value(t0, a->t, 0));
  }
  if (ui_button(rect(f->x + half + gap, f->y, half, f->rh), "LABEL IT", fl)) {
    arr_push(s->labels, ((ShotLabel){a->t, a->t + 3, scene, shot_str(s, e->fbuf_part), NULL}));
    a->sel_on = true, a->sel_what = WHAT_LABEL, a->sel = (Sel){SEL_DEVICE, -1, arr_len(s->labels) - 1};
    anim_touch(e);
  }
  f->y += f->rh + gap;
}

static void panel_video(AF *f) {
  Editor *e = f->e;
  Anim *a = &e->a;
  Shot *s = a->shot;
  afsection(f, "VIDEO");
  aftext(f, "Name", &s->name);
  float w = (f->w - f->lw - S(3)) / 2;
  double vw = s->w, vh = s->h, fps = s->fps;
  Rect r = afrow(f, "Size");
  if (ui_drag(rect(r.x, r.y, w, r.h), "Video w", &vw, 2, NULL) && vw >= 2) s->w = (int)vw & ~1, anim_touch(e);
  if (ui_drag(rect(r.x + w + S(3), r.y, w, r.h), "Video h", &vh, 2, NULL) && vh >= 2) s->h = (int)vh & ~1, anim_touch(e);
  if (ui_drag(afrow(f, "Frames/s"), "Video fps", &fps, 1, NULL) && fps >= 1) s->fps = (int)fps, anim_touch(e);
  double dur = s->duration;
  if (ui_drag(afrow(f, "Length"), "Video length", &dur, 0.1, "s") && dur >= 0.1) {
    s->duration = dur;
    if (arr_len(s->scenes)) arr_last(s->scenes).end = dur;
    a->px_per_s = 0;
    player_invalidate(a->player);
    anim_touch(e);
  }
  float half = (f->w - S(3)) / 2;
  if (ui_button(rect(f->x, f->y, half, f->rh), "SAVE VIDEO", a->dirty ? BTN_ON : 0)) anim_save(e);
  if (ui_button(rect(f->x + half + S(3), f->y, half, f->rh), "RENDER MP4", 0)) {
    char base[1100];
    snprintf(base, sizeof base, "%s", s->path[0] ? s->path : "video.shot.json");
    char *dot = strstr(base, ".shot.json");
    if (dot) *dot = 0;
    else if ((dot = strrchr(base, '.'))) *dot = 0;
    snprintf(a->out, sizeof a->out, "%.1000s.mp4", base);
    a->r_w = s->w, a->r_h = s->h, a->r_fps = s->fps, a->r_crf = 18, a->r_ss = 1.5f, a->r_grain = true;
    a->r_done = a->r_failed = a->r_cancelled = false;
    a->r_msg[0] = 0;
    e->modal = MODAL_RENDER;
  }
  f->y += f->rh + S(3);
}

static void panel(Editor *e, Rect r) {
  Anim *a = &e->a;
  ui_rect(r, RGBA(7, 12, 9, 255));
  ui_rect(rect(r.x, r.y, maxf(1, S(1)), r.h), C_LINE);
  ui_block(r);
  Scroll sc = ui_scroll_begin(r, "anim-props");
  AF f = {e, r.x + S(12), sc.y + S(2), r.w - S(24), S(86), S(24)};
  panel_video(&f);
  panel_add(&f);
  if (a->sel_on) {
    ui_push_id(a->sel_what * 100000 + (a->sel.layer + 1) * 300 + a->sel.index + 1);
    switch (a->sel_what) {
    case WHAT_KEY:
      if (a->sel.layer >= 0 && a->sel.layer < arr_len(a->shot->tracks)) panel_key(&f);
      break;
    case WHAT_CAPTION:
      if (a->sel.index < arr_len(a->shot->captions)) panel_caption(&f);
      break;
    case WHAT_LABEL:
      if (a->sel.index < arr_len(a->shot->labels)) panel_label(&f);
      break;
    case WHAT_SCENE:
      if (a->sel.index < arr_len(a->shot->scenes)) panel_scene(&f);
      break;
    }
    ui_pop_id();
  }
  ui_scroll_end(&sc, f.y + S(16));
}

// ------------------------------------------------------------------ the picture
static void preview(Editor *e, Rect area) {
  Anim *a = &e->a;
  Shot *s = a->shot;
  const UiInput *in = e->in;
  if (!a->player) return;
  float asp = scene_aspect(s);
  float pad = S(10), aw = area.w - 2 * pad, ah = area.h - 2 * pad;
  float w = aw, h = aw / asp;
  if (h > ah) h = ah, w = ah * asp;
  w = maxf(floorf(w / 2) * 2, 2), h = maxf(floorf(h / 2) * 2, 2);
  Rect view = rect(roundf(area.x + (area.w - w) / 2), roundf(area.y + (area.h - h) / 2), w, h);
  ui_mark("picture", view);
  ShotFrame f = player_seek(a->player, a->t, asp);
  a->frame = f;
  if (!a->detached) {
    a->stage.cam.pos = f.cam_pos, a->stage.cam.target = f.cam_target;
    a->stage.cam.fov_deg = f.fov > 0 ? f.fov : 40;
    a->stage.flying = false;
    a->stage.pend_theta = a->stage.pend_phi = a->stage.pend_zoom = a->stage.pend_px = a->stage.pend_py = 0;
  }
  a->stage.cam.near_z = f.near_z;
  a->stage.cam.far_z = f.far_z;
  // Orbiting leaves the track, until the next scrub.
  bool over = rect_has(view, in->mx, in->my) && !ui_wants_mouse() && e->modal == MODAL_NONE;
  if (f.model) {
    if (over && in->pressed[0]) a->press_pending = true;
    stage_input(&a->stage, in, view);
    if (over && in->wheel != 0) a->detached = true;
    if (a->stage.dragging && a->stage.moved > S(4)) {
      a->detached = true;
      a->playing = false;
    }
    if (in->released[0] && a->press_pending) {
      a->press_pending = false;
      if (a->stage.moved < S(6) && over) {
        Vec3 o, d;
        Camera c = a->stage.cam;
        camera_ray(&c, w / h, in->mx - view.x, in->my - view.y, w, h, &o, &d);
        Hit hit = model_pick(f.model, o, d);
        if (hit.node >= 0) {
          snprintf(e->fbuf_part, sizeof e->fbuf_part, "%s", f.model->nodes[hit.node].name);
          ed_status(e, false, "Picked %s", e->fbuf_part);
        } else {
          e->fbuf_part[0] = 0;
        }
      }
    }
    if (a->detached) stage_tick(&a->stage, minf(in->dt, 0.1f), (int)h);
  }
  PostParams post = post_defaults();
  post.fade = f.fade;
  post.time = (float)fmod(a->t, 100);
  ed_view_prepare(e, (int)w, (int)h);
  if (f.model) r_render(f.model, &a->stage.cam, &post, NULL, e->vfbo);
  else r_render_empty(&post, e->vfbo);
  ed_view_blit(e, view);
  // Labels and captions exactly as the video will have them.
  ui_clip_push(view);
  ui_set_origin(view.x, view.y);
  ui_set_scale(h / 900);
  ShotFrame show = f;
  if (a->detached) show.cam_pos = a->stage.cam.pos, show.cam_target = a->stage.cam.target, show.fov = a->stage.cam.fov_deg;
  shot_draw_overlay(s, &show, a->t, w, h, asp);
  ui_set_scale(in->scale);
  ui_set_origin(0, 0);
  ui_clip_pop();
  ui_border(view, a->detached ? C_ACCENT : C_LINE, maxf(1, S(1)));
}

static void empty_card(Editor *e, Rect area) {
  Doc *d = ed_doc(e);
  float W = S(420), H = S(208);
  Rect r = rect(area.x + (area.w - W) / 2, area.y + (area.h - H) / 2, W, H);
  ui_rect(r, RGBA(6, 12, 8, 240));
  ui_border(r, C_LINE, maxf(1, S(1)));
  ui_block(r);
  float pad = S(16), y = r.y + S(14);
  ui_text_sp(r.x + pad, y, S(12), FONT_BOLD, C_BRIGHT, S(2), "NO VIDEO YET");
  y += S(24);
  char msg[300];
  snprintf(msg, sizeof msg, "A video of %s: write one from what the data already says, or start with an empty timeline.", d->spec->name);
  y += ui_wrap(r.x + pad, y, W - 2 * pad, S(12), FONT_REGULAR, C_DIM, S(17), msg) + S(12);
  float bw = W - 2 * pad;
  if (ui_button(rect(r.x + pad, y, bw, S(28)), "AUTO VIDEO", BTN_ON)) new_auto(e);
  y += S(32);
  if (ui_button(rect(r.x + pad, y, bw, S(28)), "EMPTY TIMELINE", 0)) new_empty(e);
  y += S(32);
  if (ui_button(rect(r.x + pad, y, bw, S(28)), "OPEN A VIDEO", 0)) ed_modal_files(e, MODAL_OPEN);
}

// ------------------------------------------------------------------ the tab
void anim_frame(Editor *e) {
  Anim *a = &e->a;
  Rect b = ed_body(e);
  float rw = S(300), th = minf(S(260), b.h * 0.45f);
  Rect right = rect(b.x + b.w - rw, b.y, rw, b.h);
  Rect tl = rect(b.x, b.y + b.h - th, b.w - rw, th);
  Rect area = rect(b.x, b.y, b.w - rw, b.h - th);
  if (!a->shot) {
    empty_card(e, b);
    return;
  }
  anim_keys(e);
  if (a->stale || a->doc_version != docs_version(e)) rebuild_player(e);
  if (a->playing) {
    a->t += (double)minf(e->in->dt, 0.1f);
    if (a->t >= a->shot->duration) {
      a->t = a->shot->duration;
      a->playing = false;
    }
  }
  preview(e, area);
  timeline(e, tl);
  panel(e, right);
}

// ------------------------------------------------------------------ rendering
bool anim_rendering(const Editor *e) { return e->a.video != NULL; }

static void render_start(Editor *e) {
  Anim *a = &e->a;
  char err[1024] = "";
  SpecOverride *over = NULL;
  for (int i = 0; i < arr_len(e->docs); i++)
    if (e->docs[i]->path[0]) arr_push(over, ((SpecOverride){e->docs[i]->path, e->docs[i]->spec}));
  a->video_player = player_new_over(a->shot, (BuildOpts){.paint = true}, over, arr_len(over), err, sizeof err);
  arr_free(over);
  if (!a->video_player) {
    snprintf(a->r_msg, sizeof a->r_msg, "%s", err);
    a->r_failed = true;
    return;
  }
  VideoOpts o = {.out = a->out, .w = a->r_w, .h = a->r_h, .fps = a->r_fps, .crf = a->r_crf, .scale = a->r_ss, .grain = a->r_grain};
  a->video = video_begin(a->shot, a->video_player, &o, err, sizeof err);
  if (!a->video) {
    release_player(a->video_player);
    a->video_player = NULL;
    snprintf(a->r_msg, sizeof a->r_msg, "%s", err);
    a->r_failed = true;
    return;
  }
  a->r_done = a->r_failed = a->r_cancelled = false;
  a->r_msg[0] = 0;
}

static void render_stop(Editor *e, bool cancelled) {
  Anim *a = &e->a;
  const char *verr = video_error(a->video);
  if (verr) snprintf(a->r_msg, sizeof a->r_msg, "%s", verr);
  bool ok = video_end(a->video);
  a->video = NULL;
  release_player(a->video_player);
  a->video_player = NULL;
  ed_sweep_textures(e);
  if (cancelled) {
    unlink(a->out);
    a->r_cancelled = true;
    snprintf(a->r_msg, sizeof a->r_msg, "Cancelled; the partial file is gone.");
    return;
  }
  a->r_done = ok;
  a->r_failed = !ok;
  if (ok) snprintf(a->r_msg, sizeof a->r_msg, "Wrote %.240s", a->out);
  else if (!a->r_msg[0]) snprintf(a->r_msg, sizeof a->r_msg, "ffmpeg did not finish the file.");
}

void anim_render_tick(Editor *e) {
  Anim *a = &e->a;
  if (!a->video) return;
  if (!video_frame(a->video)) a->video_finished = true;
}

static void running(Editor *e) {
  Anim *a = &e->a;
  const UiInput *in = e->in;
  float W = (float)in->w, H = (float)in->h;
  // The frame itself, as large as fits above the bar.
  float asp = (float)a->r_w / (float)maxi(a->r_h, 1), ah = H - S(90), aw = W - S(40);
  float w = aw, h = aw / asp;
  if (h > ah) h = ah, w = ah * asp;
  int x = (int)((W - w) / 2), y = (int)S(16);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, video_fbo(a->video));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, e->fbo);
  glBlitFramebuffer(0, 0, a->r_w, a->r_h, x, (int)H - y - (int)h, x + (int)w, (int)H - y, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, e->fbo);
  float by = H - S(60), bx = S(20), bw = W - S(40) - S(120);
  float p = video_progress(a->video);
  ui_rect(rect(bx, by + S(18), bw, S(8)), RGBA(30, 44, 30, 255));
  ui_rect(rect(bx, by + S(18), bw * p, S(8)), C_ACCENT);
  char t[1400];
  snprintf(t, sizeof t, "Rendering %d of %d frames to %s", video_frame_index(a->video), video_frame_count(a->video), a->out);
  ui_text_fit(bx, by - S(2), bw, S(12), FONT_REGULAR, C_TEXT, t);
  Rect cr = rect(W - S(20) - S(104), by + S(2), S(104), S(28));
  ui_rect(cr, ui_hovering(cr) ? C_HOVER : RGBA(6, 12, 8, 255));
  ui_border(cr, C_LINE, maxf(1, S(1)));
  float tw = ui_text_width(S(11), FONT_REGULAR, S(1), "CANCEL");
  ui_text_sp(cr.x + (cr.w - tw) / 2, cr.y + S(6), S(11), FONT_REGULAR, C_TEXT, S(1), "CANCEL");
  ui_mark("CANCEL##render", cr);
  // The video draws its own overlay each frame, which restarts the interface: a press, not a click.
  if ((rect_has(cr, in->mx, in->my) && in->pressed[0]) || in->keys[KEY_ESCAPE]) {
    render_stop(e, true);
    e->modal = MODAL_RENDER;
    a->video_finished = false;
    return;
  }
  if (a->video_finished) {
    a->video_finished = false;
    render_stop(e, false);
    e->modal = MODAL_RENDER;
  }
}

void anim_render_modal(Editor *e) {
  Anim *a = &e->a;
  const UiInput *in = e->in;
  if (a->video) {
    running(e);
    return;
  }
  float W = S(520), H = S(300);
  Rect r = rect(((float)in->w - W) / 2, ((float)in->h - H) / 2, W, H);
  ui_set_popup(rect(0, 0, (float)in->w, (float)in->h));
  ui_set_layer(1);
  ui_rect(rect(0, 0, (float)in->w, (float)in->h), RGBA(2, 4, 3, 170));
  ui_rect(r, RGBA(6, 12, 8, 255));
  ui_border(r, C_ACCENT, maxf(1, S(1)));
  float pad = S(16), x = r.x + pad, y = r.y + S(14), w = W - 2 * pad, lw = S(96), rh = S(26);
  ui_text_sp(x, y, S(12), FONT_BOLD, C_BRIGHT, S(2), "RENDER");
  y += S(26);
#define LABEL(t) ui_text(x, y + S(5), S(11), FONT_REGULAR, C_DIM, t)
  LABEL("Size");
  static const int SIZES[][2] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
  static const char *const SN[] = {"720p", "1080p", "1440p", "4K"};
  float sw = (w - lw - 3 * S(4)) / 4;
  for (int i = 0; i < 4; i++) {
    bool on = a->r_w == SIZES[i][0] && a->r_h == SIZES[i][1];
    if (ui_button(rect(x + lw + (float)i * (sw + S(4)), y, sw, rh), SN[i], on ? BTN_ON : 0)) a->r_w = SIZES[i][0], a->r_h = SIZES[i][1];
  }
  y += rh + S(5);
  LABEL("Frames/s");
  static const int FPS[] = {24, 30, 60};
  float fw = (w - lw - 2 * S(4)) / 3;
  for (int i = 0; i < 3; i++) {
    char n[16];
    snprintf(n, sizeof n, "%d##fps", FPS[i]);
    if (ui_button(rect(x + lw + (float)i * (fw + S(4)), y, fw, rh), n, a->r_fps == FPS[i] ? BTN_ON : 0)) a->r_fps = FPS[i];
  }
  y += rh + S(5);
  LABEL("Quality");
  static const int CRF[] = {23, 18, 14};
  static const char *const QN[] = {"Draft", "Good", "Best"};
  static const float SS[] = {1, 1.5f, 2};
  for (int i = 0; i < 3; i++)
    if (ui_button(rect(x + lw + (float)i * (fw + S(4)), y, fw, rh), QN[i], a->r_crf == CRF[i] ? BTN_ON : 0)) a->r_crf = CRF[i], a->r_ss = SS[i];
  y += rh + S(5);
  LABEL("Film grain");
  if (ui_button(rect(x + lw, y, fw, rh), a->r_grain ? "ON##grain" : "OFF##grain", a->r_grain ? BTN_ON : 0)) a->r_grain = !a->r_grain;
  y += rh + S(5);
  LABEL("File");
  ui_text_field(rect(x + lw, y, w - lw, rh), "render-file", a->out, sizeof a->out, NULL);
  y += rh + S(8);
  if (a->r_msg[0]) ui_wrap(x, y, w, S(11), FONT_REGULAR, a->r_failed ? C_ERROR : C_BRIGHT, S(15), a->r_msg);
  float by = r.y + H - S(40);
  bool go = ui_button(rect(r.x + W - pad - S(110), by, S(110), S(28)), "START##render", a->out[0] ? BTN_ON : BTN_DISABLED);
  if (ui_button(rect(r.x + W - pad - S(226), by, S(110), S(28)), "CLOSE##render", 0) || (in->keys[KEY_ESCAPE] && !ui_wants_keyboard())) {
    e->modal = MODAL_NONE;
    return;
  }
  if (go) render_start(e);
}

// ------------------------------------------------------------------ for checks
void anim_query(Editor *e, const char *what, char *out, size_t n, bool *ok) {
  Anim *a = &e->a;
  *ok = true;
  if (!strcmp(what, "shot")) snprintf(out, n, "%d", a->shot != NULL);
  else if (!a->shot) *ok = false;
  else if (!strcmp(what, "t")) snprintf(out, n, "%.3f", a->t);
  else if (!strcmp(what, "tracks")) snprintf(out, n, "%d", arr_len(a->shot->tracks));
  else if (!strcmp(what, "scenes")) snprintf(out, n, "%d", arr_len(a->shot->scenes));
  else if (!strcmp(what, "captions")) snprintf(out, n, "%d", arr_len(a->shot->captions));
  else if (!strcmp(what, "labels")) snprintf(out, n, "%d", arr_len(a->shot->labels));
  else if (!strcmp(what, "duration")) snprintf(out, n, "%.3f", a->shot->duration);
  else if (!strcmp(what, "shotdirty")) snprintf(out, n, "%d", a->dirty);
  else if (!strcmp(what, "shotpath")) snprintf(out, n, "%s", a->shot->path);
  else if (!strcmp(what, "playing")) snprintf(out, n, "%d", a->playing);
  else if (!strcmp(what, "detached")) snprintf(out, n, "%d", a->detached);
  else if (!strcmp(what, "rendering")) snprintf(out, n, "%d", a->video != NULL);
  else if (!strcmp(what, "renderdone")) snprintf(out, n, "%d", a->r_done);
  else if (!strcmp(what, "seltime")) {
    if (!a->sel_on || a->sel_what != WHAT_KEY) *ok = false;
    else snprintf(out, n, "%.3f", a->shot->tracks[a->sel.layer].keys[a->sel.index].t);
  } else if (!strncmp(what, "keys:", 5)) {
    int i = atoi(what + 5);
    if (i < 0 || i >= arr_len(a->shot->tracks)) *ok = false;
    else snprintf(out, n, "%d", arr_len(a->shot->tracks[i].keys));
  } else {
    *ok = false;
  }
}
