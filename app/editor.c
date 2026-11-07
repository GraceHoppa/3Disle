#include "app.h"
#include "editor_int.h"
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>

// ------------------------------------------------------------------ undo
static char *dup_str(const char *s) {
  size_t n = strlen(s) + 1;
  char *d = malloc(n);
  memcpy(d, s, n);
  return d;
}

void hist_init(History *h, const char *text) {
  hist_free(h);
  h->base = dup_str(text);
}
void hist_free(History *h) {
  for (int i = 0; i < arr_len(h->undo); i++) free(h->undo[i].text);
  for (int i = 0; i < arr_len(h->redo); i++) free(h->redo[i].text);
  arr_free(h->undo);
  arr_free(h->redo);
  free(h->base);
  memset(h, 0, sizeof *h);
}
static void clear_redo(History *h) {
  for (int i = 0; i < arr_len(h->redo); i++) free(h->redo[i].text);
  arr_clear(h->redo);
}
void hist_changed(History *h, Sel sel) {
  if (h->open) return;
  h->open = true;
  clear_redo(h);
  arr_push(h->undo, ((Snap){dup_str(h->base), sel}));
  // A long session keeps what can be undone in reach, not every step since morning.
  if (arr_len(h->undo) > 300) {
    free(h->undo[0].text);
    memmove(h->undo, h->undo + 1, sizeof(Snap) * (size_t)(arr_len(h->undo) - 1));
    arr_pop(h->undo);
  }
}
void hist_settle(History *h, const char *text) {
  h->open = false;
  free(h->base);
  h->base = dup_str(text);
}
const char *hist_undo(History *h, Sel *sel, Sel now) {
  if (!arr_len(h->undo)) return NULL;
  Snap s = arr_last(h->undo);
  arr_pop(h->undo);
  arr_push(h->redo, ((Snap){h->base, now}));
  h->base = s.text;
  *sel = s.sel;
  return h->base;
}
const char *hist_redo(History *h, Sel *sel, Sel now) {
  if (!arr_len(h->redo)) return NULL;
  Snap s = arr_last(h->redo);
  arr_pop(h->redo);
  arr_push(h->undo, ((Snap){h->base, now}));
  h->base = s.text;
  *sel = s.sel;
  return h->base;
}

// ------------------------------------------------------------------ small things
void ed_status(Editor *e, bool err, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e->status, sizeof e->status, fmt, ap);
  va_end(ap);
  e->status_at = e->clock;
  e->status_err = err;
}

static float view_scale(const UiInput *in) { return in->scale >= 1.5f ? 1.0f : 1.5f; }

// Everything 3D in the editor is drawn at the size of the free area, then copied to the window.
void ed_view_prepare(Editor *e, int w, int h) {
  w = maxi(w, 2), h = maxi(h, 2);
  if (e->vw != w || e->vh != h) {
    r_free_offscreen(e->vfbo);
    e->vfbo = r_offscreen_fbo(w, h);
    e->vw = w, e->vh = h;
  }
  r_set_size(w, h, view_scale(e->in), 4);
}

void ed_view_blit(Editor *e, Rect r) {
  int H = e->in->h, x = (int)lroundf(r.x), y = (int)lroundf(r.y);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, e->vfbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, e->fbo);
  glDisable(GL_SCISSOR_TEST);
  glBlitFramebuffer(0, 0, e->vw, e->vh, x, H - y - e->vh, x + e->vw, H - y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, e->fbo);
}

Rect ed_body(const Editor *e) {
  float top = S(38), bottom = S(24);
  return rect(0, top, (float)e->in->w, (float)e->in->h - top - bottom);
}

// ------------------------------------------------------------------ documents
static void doc_free(Doc *d) {
  if (d->model) {
    r_release_model(d->model);
    model_free(d->model);
  }
  spec_free(d->built);
  spec_free(d->spec);
  hist_free(&d->hist);
  free(d->saved);
  free(d);
}

static void doc_mark_saved(Doc *d) {
  free(d->saved);
  d->saved = spec_write(d->spec);
  d->dirty = false;
}

static void texture_mark(Model *m) {
  if (!m) return;
  for (int i = 0; i < arr_len(m->nodes); i++)
    for (int k = 0; k < m->nodes[i].nmats; k++)
      if (m->nodes[i].mats[k].tex) m->nodes[i].mats[k].tex->marked = true;
}

// Painted surfaces are cached by what they show, so a moved chip leaves its old board behind.
void ed_sweep_textures(Editor *e) {
  texture_cache_clear_marks();
  for (int i = 0; i < arr_len(e->docs); i++) texture_mark(e->docs[i]->model);
  if (e->a.player)
    for (int i = 0; i < arr_len(e->a.player->models); i++) texture_mark(e->a.player->models[i]);
  if (e->a.video_player)
    for (int i = 0; i < arr_len(e->a.video_player->models); i++) texture_mark(e->a.video_player->models[i]);
  if (e->viewer) texture_mark(viewer_model(e->viewer));
  texture_cache_sweep(r_release_texture);
}

void ed_doc_rebuild(Editor *e, Doc *d) {
  AssemblySpec *built = spec_clone(d->spec);
  if (!built) return;
  Model *m = model_build(built, (BuildOpts){.paint = true});
  if (d->model) {
    model_start_values(m, d->model->values, arr_len(d->model->values));
    r_release_model(d->model);
    model_free(d->model);
    spec_free(d->built);
  } else {
    model_start_values(m, NULL, 0);
  }
  d->model = m;
  d->built = built;
  d->stale = false;
  d->built_at = e->clock;
  model_set_explode(m, d->explode);
  model_update(m);
  e->hover_node = e->hover_copy = -1;
  ed_sweep_textures(e);
}

void ed_doc_changed(Editor *e) { e->changed = true; }

static void doc_settle(Editor *e, Doc *d) {
  char *text = spec_write(d->spec);
  hist_settle(&d->hist, text);
  d->dirty = !d->saved || strcmp(text, d->saved) != 0;
  free(text);
}

static Doc *doc_add(Editor *e, AssemblySpec *s, const char *path) {
  Doc *d = calloc(1, sizeof *d);
  d->spec = s;
  if (path) snprintf(d->path, sizeof d->path, "%s", path);
  stage_init(&d->stage);
  char *text = spec_write(s);
  hist_init(&d->hist, text);
  if (path) d->saved = text;
  else {
    free(text);
    d->dirty = true;
  }
  arr_push(e->docs, d); // first, so the sweep after building sees this document's textures as used
  e->cur = arr_len(e->docs) - 1;
  ed_doc_rebuild(e, d);
  return d;
}

bool ed_doc_save(Editor *e, Doc *d) {
  if (!d->path[0]) {
    ed_modal_files(e, MODAL_SAVE_AS);
    return false;
  }
  if (!spec_save(d->spec, d->path)) {
    ed_status(e, true, "Couldn't write %s", d->path);
    return false;
  }
  doc_mark_saved(d);
  ed_status(e, false, "Saved %s", d->path);
  return true;
}

// Which open document holds this file, or -1.
static int doc_find(const Editor *e, const char *path) {
  for (int i = 0; i < arr_len(e->docs); i++)
    if (e->docs[i]->path[0] && path_same(e->docs[i]->path, path)) return i;
  return -1;
}

static const char *base_name(const char *p) {
  const char *s = strrchr(p, '/');
  return s ? s + 1 : p;
}

// The kind of file is what it says inside: tours have levels, videos have scenes.
int ed_open_file(Editor *e, const char *path, char *err, size_t errlen) {
  int have = doc_find(e, path);
  if (have >= 0) {
    e->cur = have;
    return have;
  }
  char *text = file_read(path, NULL);
  if (!text) {
    snprintf(err, errlen, "can't read %s", path);
    return -1;
  }
  Arena ar = {0};
  Json *j = json_parse(&ar, text, err, errlen);
  bool is_tour = j && json_get(j, "levels"), is_shot = j && json_get(j, "scenes");
  arena_free(&ar);
  if (is_shot) {
    free(text);
    if (!anim_open(e, path, err, errlen)) return -1;
    e->tab = TAB_ANIMATE;
    // The first scene is the device to work on.
    Shot *s = e->a.shot;
    if (arr_len(s->scenes)) {
      Arena a2 = {0};
      const char *full = path_join(&a2, s->path, s->scenes[0].file);
      int r = ed_open_file(e, full, err, errlen);
      arena_free(&a2);
      return r;
    }
    return e->cur;
  }
  if (is_tour) {
    if (e->tour) {
      hist_free(&e->tour->hist);
      free(e->tour->saved);
      arena_free(&e->tour->ar);
      free(e->tour);
    }
    TourDoc *t = calloc(1, sizeof *t);
    snprintf(t->path, sizeof t->path, "%s", path);
    t->root = json_parse(&t->ar, text, err, errlen);
    free(text);
    t->saved = json_write(t->root); // what "unchanged" looks like, however the file happens to be laid out
    hist_init(&t->hist, t->saved);
    e->tour = t;
    const Json *lv = json_get(t->root, "levels");
    int first = -1;
    Arena a2 = {0};
    for (const Json *l = lv->head; l; l = l->next) {
      const char *full = path_join(&a2, path, json_str(l, "file", ""));
      char lerr[256] = "";
      int r = ed_open_file(e, full, lerr, sizeof lerr);
      if (r < 0) ed_status(e, true, "%s", lerr);
      else if (first < 0) first = r;
    }
    arena_free(&a2);
    if (first < 0) {
      snprintf(err, errlen, "no level of %s opens", base_name(path));
      return -1;
    }
    e->cur = first;
    ed_doc(e)->sel = (Sel){SEL_TOUR, -1, -1};
    return first;
  }
  free(text);
  AssemblySpec *s = spec_load(path, err, errlen);
  if (!s) return -1;
  doc_add(e, s, path);
  return e->cur;
}

static void new_device(Editor *e) {
  AssemblySpec *s = spec_new("New device", 100, 60);
  spec_add_layer(s, 0, "Board");
  doc_add(e, s, NULL);
  ed_doc(e)->sel = (Sel){SEL_DEVICE, -1, -1};
}

bool editor_open(Editor *e, const char *path, char *err, size_t errlen) {
  if (!path) {
    new_device(e);
    return true;
  }
  return ed_open_file(e, path, err, errlen) >= 0;
}

Editor *editor_new(const char *root) {
  Editor *e = calloc(1, sizeof *e);
  snprintf(e->root, sizeof e->root, "%s", root);
  e->hover_node = e->hover_copy = -1;
  e->viewer_doc = -1;
  e->a.px_per_s = 0;
  e->a.add_part = -1;
  snprintf(e->dir, sizeof e->dir, "%s/models", root);
  return e;
}

void editor_free(Editor *e) {
  anim_free(e);
  if (e->viewer) viewer_free(e->viewer);
  for (int i = 0; i < arr_len(e->docs); i++) doc_free(e->docs[i]);
  arr_free(e->docs);
  if (e->tour) {
    hist_free(&e->tour->hist);
    free(e->tour->saved);
    arena_free(&e->tour->ar);
    free(e->tour);
  }
  r_free_offscreen(e->vfbo);
  free(e);
}

bool editor_should_close(const Editor *e) { return e->quit_now; }

static bool anything_unsaved(const Editor *e) {
  for (int i = 0; i < arr_len(e->docs); i++)
    if (e->docs[i]->dirty) return true;
  return e->a.dirty || (e->tour && e->tour->dirty);
}

bool editor_request_close(Editor *e) {
  if (anim_rendering(e)) return false;
  if (!anything_unsaved(e)) return e->quit_now = true;
  e->tab = TAB_BUILD; // the View tab is the viewer's whole screen, with no room to ask
  e->modal = MODAL_QUIT;
  return false;
}

// ------------------------------------------------------------------ the file dialog
static int cmp_entry(const void *a, const void *b) {
  const char *x = *(char *const *)a, *y = *(char *const *)b;
  bool dx = x[strlen(x) - 1] == '/', dy = y[strlen(y) - 1] == '/';
  if (dx != dy) return dx ? -1 : 1;
  return strcmp(x, y);
}

static void list_dir(Editor *e) {
  for (int i = 0; i < arr_len(e->entries); i++) free(e->entries[i]);
  arr_clear(e->entries);
  e->list_scroll = 0;
  DIR *d = opendir(e->dir);
  if (!d) return;
  struct dirent *en;
  while ((en = readdir(d))) {
    if (en->d_name[0] == '.') continue;
    char full[2048];
    snprintf(full, sizeof full, "%s/%s", e->dir, en->d_name);
    struct stat st;
    if (stat(full, &st)) continue;
    char name[512];
    if (S_ISDIR(st.st_mode)) snprintf(name, sizeof name, "%s/", en->d_name);
    else if (strstr(en->d_name, ".json")) snprintf(name, sizeof name, "%s", en->d_name);
    else continue;
    arr_push(e->entries, dup_str(name));
  }
  closedir(d);
  if (arr_len(e->entries)) qsort(e->entries, (size_t)arr_len(e->entries), sizeof(char *), cmp_entry);
}

void ed_modal_files(Editor *e, Modal m) {
  e->modal = m;
  list_dir(e);
  Doc *d = ed_doc(e);
  if (m == MODAL_SAVE_AS) {
    if (e->modal_for_video && e->a.shot && e->a.shot->path[0]) snprintf(e->file_field, sizeof e->file_field, "%s", base_name(e->a.shot->path));
    else if (e->modal_for_video) snprintf(e->file_field, sizeof e->file_field, "video.shot.json");
    else if (d && d->path[0]) snprintf(e->file_field, sizeof e->file_field, "%s", base_name(d->path));
    else snprintf(e->file_field, sizeof e->file_field, "device.json");
  } else {
    e->file_field[0] = 0;
  }
}

static void dir_up(Editor *e) {
  char *s = strrchr(e->dir, '/');
  if (s && s != e->dir && strcmp(s, "/..")) {
    *s = 0;
  } else {
    size_t n = strlen(e->dir);
    if (n + 4 < sizeof e->dir) snprintf(e->dir + n, sizeof e->dir - n, "/..");
  }
  list_dir(e);
}

static void modal_back(Editor *e, Rect r) {
  ui_set_popup(rect(0, 0, (float)e->in->w, (float)e->in->h));
  ui_set_layer(1);
  ui_rect(rect(0, 0, (float)e->in->w, (float)e->in->h), RGBA(2, 4, 3, 170));
  ui_rect(r, RGBA(6, 12, 8, 255));
  ui_border(r, C_ACCENT, maxf(1, S(1)));
}

static void files_finish(Editor *e) {
  char full[2200], err[512] = "";
  if (e->file_field[0] == '/') snprintf(full, sizeof full, "%s", e->file_field);
  else snprintf(full, sizeof full, "%s/%s", e->dir, e->file_field);
  if (e->modal == MODAL_OPEN) {
    if (ed_open_file(e, full, err, sizeof err) < 0) {
      ed_status(e, true, "%s", err);
      return;
    }
    e->modal = MODAL_NONE;
    e->modal_for_video = false;
    return;
  }
  if (!strstr(full, ".json")) {
    size_t n = strlen(full);
    snprintf(full + n, sizeof full - n, ".json");
  }
  if (e->modal_for_video) {
    snprintf(e->a.shot->path, sizeof e->a.shot->path, "%s", full);
    anim_save(e);
  } else {
    Doc *d = ed_doc(e);
    if (d) {
      snprintf(d->path, sizeof d->path, "%s", full);
      ed_doc_save(e, d);
    }
  }
  e->modal = MODAL_NONE;
  e->modal_for_video = false;
}

static void modal_files(Editor *e) {
  float W = minf(S(620), (float)e->in->w - S(40)), H = minf(S(520), (float)e->in->h - S(40));
  Rect r = rect(((float)e->in->w - W) / 2, ((float)e->in->h - H) / 2, W, H);
  modal_back(e, r);
  bool open = e->modal == MODAL_OPEN;
  float pad = S(16), x = r.x + pad, w = W - 2 * pad, y = r.y + S(14);
  const char *title = open ? "OPEN" : e->modal_for_video ? "SAVE VIDEO AS" : "SAVE DEVICE AS";
  ui_text_sp(x, y, S(12), FONT_BOLD, C_BRIGHT, S(2), title);
  y += S(24);
  if (ui_button(rect(x, y, S(34), S(24)), "UP##files", 0)) dir_up(e);
  ui_text_fit(x + S(44), y + S(4), w - S(44), S(11.5f), FONT_REGULAR, C_DIM, e->dir);
  y += S(32);
  float list_h = r.y + H - y - S(84);
  Rect lr = rect(x, y, w, list_h);
  ui_rect(lr, C_FIELD);
  ui_border(lr, C_LINE, maxf(1, S(1)));
  Scroll sc = ui_scroll_begin(rect_inset(lr, 1), "files-list");
  float ry = sc.y;
  int pick_dir = -1;
  for (int i = 0; i < arr_len(e->entries); i++) {
    Rect row = rect(lr.x + 1, ry, lr.w - 2, S(24));
    ui_push_id(i);
    char lbl[640];
    snprintf(lbl, sizeof lbl, "%s##file", e->entries[i]);
    bool isdir = e->entries[i][strlen(e->entries[i]) - 1] == '/';
    if (ui_button(row, lbl, BTN_LEFT | BTN_QUIET | (!strcmp(e->entries[i], e->file_field) ? BTN_ON : 0))) {
      if (isdir) pick_dir = i;
      else {
        bool again = !strcmp(e->file_field, e->entries[i]);
        snprintf(e->file_field, sizeof e->file_field, "%s", e->entries[i]);
        if (open && again) files_finish(e);
      }
    }
    ui_pop_id();
    ry += S(24);
  }
  ui_scroll_end(&sc, ry);
  if (pick_dir >= 0) {
    size_t n = strlen(e->dir);
    char *name = e->entries[pick_dir];
    name[strlen(name) - 1] = 0;
    if (n + strlen(name) + 2 < sizeof e->dir) snprintf(e->dir + n, sizeof e->dir - n, "/%s", name);
    list_dir(e);
  }
  if (e->modal == MODAL_NONE) return;
  y += list_h + S(10);
  ui_text_field(rect(x, y, w, S(26)), "file-field", e->file_field, sizeof e->file_field, open ? "file name, or a full path" : "file name");
  y += S(36);
  bool go = ui_button(rect(r.x + W - pad - S(100), y, S(100), S(26)), open ? "OPEN##go" : "SAVE##go", e->file_field[0] ? BTN_ON : BTN_DISABLED);
  if (ui_button(rect(r.x + W - pad - S(208), y, S(100), S(26)), "CANCEL##files", 0) || (e->in->keys[KEY_ESCAPE] && !ui_wants_keyboard())) {
    e->modal = MODAL_NONE;
    e->modal_for_video = false;
    go = false;
  }
  if (go || (e->in->keys[KEY_ENTER] && e->file_field[0])) files_finish(e);
}

static void close_doc_now(Editor *e, int i) {
  doc_free(e->docs[i]);
  arr_remove(e->docs, i);
  e->a.stale = true;
  e->viewer_doc = -1;
  if (!arr_len(e->docs)) new_device(e);
  e->cur = mini(e->cur, arr_len(e->docs) - 1);
}

static void modal_ask(Editor *e) {
  bool quit = e->modal == MODAL_QUIT;
  float W = S(460), H = S(150);
  Rect r = rect(((float)e->in->w - W) / 2, ((float)e->in->h - H) / 2, W, H);
  modal_back(e, r);
  float pad = S(16), y = r.y + S(16);
  ui_text_sp(r.x + pad, y, S(12), FONT_BOLD, C_BRIGHT, S(2), "UNSAVED WORK");
  y += S(26);
  char msg[1200];
  if (quit) snprintf(msg, sizeof msg, "Something here has not been saved. Save everything before closing?");
  else snprintf(msg, sizeof msg, "%s has changes that are not saved.", e->docs[e->close_doc]->spec->name);
  ui_wrap(r.x + pad, y, W - 2 * pad, S(12), FONT_REGULAR, C_TEXT, S(17), msg);
  float by = r.y + H - S(42), bw = S(120);
  bool save = ui_button(rect(r.x + W - pad - bw, by, bw, S(26)), quit ? "SAVE ALL" : "SAVE##ask", BTN_ON);
  bool drop = ui_button(rect(r.x + W - pad - 2 * bw - S(8), by, bw, S(26)), "DISCARD", 0);
  bool cancel = ui_button(rect(r.x + W - pad - 3 * bw - S(16), by, bw, S(26)), "CANCEL##ask", 0) || e->in->keys[KEY_ESCAPE];
  if (cancel) {
    e->modal = MODAL_NONE;
    return;
  }
  if (!save && !drop) return;
  e->modal = MODAL_NONE;
  if (quit) {
    bool ok = true;
    if (save) {
      for (int i = 0; i < arr_len(e->docs); i++) {
        if (!e->docs[i]->dirty) continue;
        if (!e->docs[i]->path[0]) {
          e->cur = i;
          ok = false;
          break;
        }
        ok = ed_doc_save(e, e->docs[i]) && ok;
      }
      if (!ok && !e->docs[e->cur]->path[0]) {
        ed_modal_files(e, MODAL_SAVE_AS);
        return;
      }
      if (e->a.dirty) ok = anim_save(e) && ok;
      if (e->tour && e->tour->dirty) ok = ed_tour_save(e) && ok;
    }
    if (ok) e->quit_now = true;
    return;
  }
  int i = e->close_doc;
  if (save) {
    e->cur = i;
    if (!ed_doc_save(e, e->docs[i])) return;
  }
  close_doc_now(e, i);
}

// ------------------------------------------------------------------ the bar on top
static void tabs(Editor *e, float cy) {
  static const char *const NAMES[] = {"BUILD", "ANIMATE", "VIEW"};
  float tw = S(84), th = S(26), x = ((float)e->in->w - tw * 3 - S(8)) / 2, y = cy - th / 2;
  ui_block(rect(x, y, tw * 3 + S(8), th));
  for (int i = 0; i < 3; i++) {
    if (ui_button(rect(x + (float)i * (tw + S(4)), y, tw, th), NAMES[i], e->tab == (Tab)i ? BTN_ON : 0)) e->tab = (Tab)i;
  }
}

static void doc_label(const Doc *d, char *out, size_t n) { snprintf(out, n, "%s%.150s", d->dirty ? "\xE2\x80\xA2 " : "", d->path[0] ? base_name(d->path) : d->spec->name); }

static void editor_save(Editor *e) {
  Doc *d = ed_doc(e);
  if (e->tab == TAB_ANIMATE && e->a.shot) {
    anim_save(e);
    return;
  }
  if (d) ed_doc_save(e, d);
  if (e->tour && e->tour->dirty) ed_tour_save(e);
}

// What is selected has to exist: undo can bring back a document without it.
void ed_sel_valid(Doc *d) {
  const AssemblySpec *s = d->spec;
  Sel *c = &d->sel;
  bool ok = true;
  switch (c->kind) {
  case SEL_LAYER: ok = c->layer >= 0 && c->layer < arr_len(s->layers); break;
  case SEL_PART: ok = c->layer >= 0 && c->layer < arr_len(s->layers) && c->index >= 0 && c->index < arr_len(s->layers[c->layer].parts); break;
  case SEL_LINK: ok = c->layer >= 0 && c->layer < arr_len(s->layers) && c->index >= 0 && c->index < arr_len(s->layers[c->layer].links); break;
  case SEL_WIRE: ok = c->index >= 0 && c->index < arr_len(s->wires); break;
  case SEL_CONTROL: ok = c->index >= 0 && c->index < arr_len(s->controls); break;
  case SEL_READOUT: ok = c->index >= 0 && c->index < arr_len(s->readouts); break;
  default: break;
  }
  if (!ok) *c = (Sel){SEL_DEVICE, -1, -1};
}

// One page from what is on disk, so what the page shows is what was saved.
static void editor_export(Editor *e) {
  Doc *d = ed_doc(e);
  bool tour = e->tour && (d->sel.kind == SEL_TOUR || d->sel.kind == SEL_LEVEL);
  if (tour && e->tour->dirty && !ed_tour_save(e)) return;
  for (int i = 0; i < arr_len(e->docs); i++) {
    Doc *o = e->docs[i];
    if (!o->dirty) continue;
    if (!o->path[0]) {
      e->cur = i;
      ed_status(e, true, "Save this device first: the page is made from the files");
      ed_doc_save(e, o);
      return;
    }
    if (!ed_doc_save(e, o)) return;
  }
  const char *entry = tour ? e->tour->path : d->path;
  char out[1100], err[1024] = "";
  snprintf(out, sizeof out, "%s", entry);
  char *dot = strrchr(out, '.');
  if (dot && !strchr(dot, '/')) *dot = 0;
  size_t n = strlen(out);
  snprintf(out + n, sizeof out - n, ".html");
  if (export_page(entry, out, err, sizeof err)) ed_status(e, false, "Wrote %s: one file, opens in any browser", out);
  else ed_status(e, true, "%s", err);
}

static void apply_doc_text(Doc *d, const char *text, Sel sel) {
  char err[256];
  AssemblySpec *s = spec_parse(text, err, sizeof err);
  if (!s) return;
  spec_free(d->spec);
  d->spec = s;
  d->stale = true;
  d->version++;
  d->sel = sel;
  ed_sel_valid(d);
  d->dirty = !d->saved || strcmp(text, d->saved) != 0;
}

static void editor_undo(Editor *e, bool redo) {
  ui_unfocus();
  if (e->tab == TAB_ANIMATE && e->a.shot) {
    anim_undo(e, redo);
    return;
  }
  Doc *d = ed_doc(e);
  if (!d) return;
  if ((d->sel.kind == SEL_TOUR || d->sel.kind == SEL_LEVEL) && ed_tour_undo(e, redo)) {
    ed_status(e, false, redo ? "Redone" : "Undone");
    return;
  }
  if (d->hist.open) doc_settle(e, d);
  Sel sel;
  const char *text = redo ? hist_redo(&d->hist, &sel, d->sel) : hist_undo(&d->hist, &sel, d->sel);
  if (!text) {
    ed_status(e, false, "Nothing to %s", redo ? "redo" : "undo");
    return;
  }
  apply_doc_text(d, text, sel);
  ed_status(e, false, redo ? "Redone" : "Undone");
}

static void top_bar(Editor *e) {
  float W = (float)e->in->w, h = S(38);
  Rect bar = rect(0, 0, W, h);
  ui_rect(bar, RGBA(5, 9, 7, 255));
  ui_rect(rect(0, h - maxf(1, S(1)), W, maxf(1, S(1))), C_LINE);
  ui_block(bar);
  ui_text_sp(S(14), S(11), S(12), FONT_BOLD, C_BRIGHT, S(2.4f), "3DISLE");
  float x = S(92);
  // The open documents.
  int n = arr_len(e->docs);
  char names[32][160];
  const char *opts[32];
  for (int i = 0; i < n && i < 32; i++) {
    doc_label(e->docs[i], names[i], sizeof names[i]);
    opts[i] = names[i];
  }
  int sel = e->cur;
  if (ui_dropdown(rect(x, S(6), S(190), S(26)), "documents", &sel, opts, mini(n, 32))) {
    e->cur = sel;
    e->part_drag = false;
    e->hover_node = -1;
    e->a.stale = true;
  }
  x += S(194);
  if (ui_button(rect(x, S(6), S(26), S(26)), "\xC3\x97##close", 0)) {
    if (ed_doc(e)->dirty) {
      e->close_doc = e->cur;
      e->modal = MODAL_CLOSE;
    } else {
      close_doc_now(e, e->cur);
    }
  }
  tabs(e, h / 2);
  float bw = S(56), bx = W - S(10) - bw;
  if (ui_button(rect(bx, S(6), bw, S(26)), "REDO", 0)) editor_undo(e, true);
  bx -= bw + S(4);
  if (ui_button(rect(bx, S(6), bw, S(26)), "UNDO", 0)) editor_undo(e, false);
  bx -= bw + S(12);
  if (ui_button(rect(bx, S(6), bw, S(26)), "EXPORT", 0)) editor_export(e);
  bx -= bw + S(4);
  if (ui_button(rect(bx, S(6), bw, S(26)), "SAVE", 0)) editor_save(e);
  bx -= bw + S(4);
  if (ui_button(rect(bx, S(6), bw, S(26)), "OPEN", 0)) ed_modal_files(e, MODAL_OPEN);
  bx -= bw + S(4);
  if (ui_button(rect(bx, S(6), bw, S(26)), "NEW", 0)) new_device(e);
}

static void status_bar(Editor *e) {
  float W = (float)e->in->w, H = (float)e->in->h, h = S(24);
  Rect bar = rect(0, H - h, W, h);
  ui_rect(bar, RGBA(5, 9, 7, 255));
  ui_rect(rect(0, H - h, W, maxf(1, S(1))), C_LINE);
  ui_block(bar);
  Doc *d = ed_doc(e);
  float y = H - h + (h - ui_line_height(S(11))) / 2;
  bool fresh = e->status[0] && e->clock - e->status_at < 6;
  if (fresh) {
    ui_text_fit(S(14), y, W * 0.55f, S(11), FONT_REGULAR, e->status_err ? C_ERROR : C_BRIGHT, e->status);
  } else if (d && d->model && arr_len(d->model->missing)) {
    char msg[300];
    snprintf(msg, sizeof msg, "A wire or link names nothing: %s", d->model->missing[0]);
    ui_text_fit(S(14), y, W * 0.55f, S(11), FONT_REGULAR, C_ERROR, msg);
  } else {
    const char *hint = e->tab == TAB_BUILD ? "Click to select \xC2\xB7 drag a selected part to move it \xC2\xB7 drag elsewhere to orbit \xC2\xB7 shift or right drag to pan \xC2\xB7 F frames it"
                                           : "Space plays \xC2\xB7 K sets a key \xC2\xB7 drag the ruler to scrub \xC2\xB7 double-click a row to add a key \xC2\xB7 wheel zooms the timeline";
    ui_text_fit(S(14), y, W * 0.7f, S(11), FONT_REGULAR, C_FAINT, hint);
  }
  const char *right = d ? (d->path[0] ? d->path : "not saved yet") : "";
  float rw = minf(ui_text_width(S(11), FONT_REGULAR, 0, right), W * 0.28f);
  ui_text_fit(W - rw - S(14), y, rw + 1, S(11), FONT_REGULAR, C_FAINT, right);
}

// ------------------------------------------------------------------ the frame
static void view_overlay(void *ud) {
  Editor *e = ud;
  tabs(e, S(19));
}

static void view_frame(Editor *e, const UiInput *in, u32 fbo) {
  Doc *d = ed_doc(e);
  if (!e->viewer) {
    e->viewer = viewer_new(e->root, true);
    viewer_set_overlay(e->viewer, view_overlay, e);
    viewer_set_intro(e->viewer, false);
  }
  int idx = e->cur;
  if (e->viewer_doc != idx || e->viewer_version != d->version) {
    r_set_size(in->w, in->h, view_scale(in), 4);
    viewer_open_doc(e->viewer, d->spec, d->path[0] ? d->path : "unsaved.json", stage_aspect(in));
    e->viewer_doc = idx;
    e->viewer_version = d->version;
    ed_sweep_textures(e);
  }
  r_set_size(in->w, in->h, view_scale(in), 4);
  viewer_frame(e->viewer, in, fbo);
}

static void keys(Editor *e) {
  const UiInput *in = e->in;
  if (!in->ctrl) return;
  if (in->keys[KEY_S]) editor_save(e);
  if (in->keys[KEY_O]) ed_modal_files(e, MODAL_OPEN);
  if (in->keys[KEY_N]) new_device(e);
  if (in->keys[KEY_Z]) editor_undo(e, in->shift);
  if (in->keys[KEY_Y]) editor_undo(e, true);
  if (in->keys[KEY_1]) e->tab = TAB_BUILD;
  if (in->keys[KEY_2]) e->tab = TAB_ANIMATE;
  if (in->keys[KEY_3]) e->tab = TAB_VIEW;
}

// The end of a frame is where a burst of edits becomes one undo step and reaches the model.
static void finish_edits(Editor *e) {
  Doc *d = ed_doc(e);
  if (!d) return;
  if (e->changed) {
    hist_changed(&d->hist, e->sel_before);
    d->stale = true;
    d->version++;
    d->dirty = true;
    e->changed = false;
  }
  if (d->hist.open && !e->in->down[0] && !ui_wants_keyboard()) doc_settle(e, d);
  ed_tour_settle(e);
}

void editor_frame(Editor *e, const UiInput *in, u32 fbo) {
  e->in_copy = *in;
  e->in = &e->in_copy;
  e->fbo = fbo;
  e->clock += (double)in->dt;
  if (!arr_len(e->docs)) new_device(e);
  e->sel_before = ed_doc(e)->sel; // undo goes back to what was selected before the change, not after
  if (anim_rendering(e)) {
    // The render takes over: one frame of video per frame of interface.
    anim_render_tick(e);
    ui_begin(in);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, in->w, in->h);
    glClearColor(0.016f, 0.027f, 0.039f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    anim_render_modal(e);
    ui_render(fbo);
    return;
  }
  // Keyboard first: while a modal is up only it listens.
  if (e->tab == TAB_VIEW) {
    if (in->ctrl) {
      keys(e);
    }
    if (e->tab == TAB_VIEW) {
      view_frame(e, in, fbo);
      finish_edits(e);
      return;
    }
  }
  ui_begin(in);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, in->w, in->h);
  glClearColor(0.016f, 0.027f, 0.039f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  if (e->modal == MODAL_NONE) keys(e);
  if (e->tab == TAB_BUILD) build_frame(e);
  else if (e->tab == TAB_ANIMATE) anim_frame(e);
  top_bar(e);
  status_bar(e);
  switch (e->modal) {
  case MODAL_OPEN:
  case MODAL_SAVE_AS: modal_files(e); break;
  case MODAL_CLOSE:
  case MODAL_QUIT: modal_ask(e); break;
  case MODAL_RENDER: anim_render_modal(e); break;
  default: break;
  }
  ui_set_layer(0);
  ui_render(fbo);
  finish_edits(e);
  if (e->a.shot) anim_settle(e);
}

// ------------------------------------------------------------------ for checks
bool editor_query(Editor *e, const char *what, char *out, size_t n) {
  Doc *d = ed_doc(e);
  out[0] = 0;
  static const char *const TABS[] = {"build", "animate", "view"};
  static const char *const KINDS[] = {"device", "layer", "part", "link", "wire", "control", "readout", "tour", "level"};
  if (!strcmp(what, "tab")) snprintf(out, n, "%s", TABS[e->tab]);
  else if (!strcmp(what, "name")) snprintf(out, n, "%s", d->spec->name);
  else if (!strcmp(what, "path")) snprintf(out, n, "%s", d->path);
  else if (!strcmp(what, "docs")) snprintf(out, n, "%d", arr_len(e->docs));
  else if (!strcmp(what, "dirty")) snprintf(out, n, "%d", d->dirty);
  else if (!strcmp(what, "undo")) snprintf(out, n, "%d", arr_len(d->hist.undo));
  else if (!strcmp(what, "redo")) snprintf(out, n, "%d", arr_len(d->hist.redo));
  else if (!strcmp(what, "layers")) snprintf(out, n, "%d", arr_len(d->spec->layers));
  else if (!strcmp(what, "wires")) snprintf(out, n, "%d", arr_len(d->spec->wires));
  else if (!strcmp(what, "controls")) snprintf(out, n, "%d", arr_len(d->spec->controls));
  else if (!strcmp(what, "parts")) {
    int c = 0;
    for (int i = 0; i < arr_len(d->spec->layers); i++) c += arr_len(d->spec->layers[i].parts);
    snprintf(out, n, "%d", c);
  } else if (!strcmp(what, "nodes")) snprintf(out, n, "%d", arr_len(d->model->nodes));
  else if (!strcmp(what, "sel")) snprintf(out, n, "%s", KINDS[d->sel.kind]);
  else if (!strcmp(what, "selname")) {
    const char *name = "";
    if (d->sel.kind == SEL_PART) name = d->spec->layers[d->sel.layer].parts[d->sel.index].name;
    else if (d->sel.kind == SEL_LAYER) name = d->spec->layers[d->sel.layer].name;
    else if (d->sel.kind == SEL_WIRE) name = d->spec->wires[d->sel.index].name;
    snprintf(out, n, "%s", name);
  } else if (!strcmp(what, "at")) {
    if (d->sel.kind != SEL_PART) return false;
    const PartSpec *p = &d->spec->layers[d->sel.layer].parts[d->sel.index];
    snprintf(out, n, "%.4g,%.4g", p->at.x, p->at.y);
  } else if (!strcmp(what, "modal")) snprintf(out, n, "%d", e->modal);
  else if (!strcmp(what, "missing")) snprintf(out, n, "%d", arr_len(d->model->missing));
  else if (!strcmp(what, "status")) snprintf(out, n, "%s", e->status);
  else {
    bool ok = false;
    anim_query(e, what, out, n, &ok);
    return ok;
  }
  return true;
}
