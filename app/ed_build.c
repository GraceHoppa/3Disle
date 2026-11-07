// The Build tab: the tree of what the device is made of, the 3D view, moving parts by hand.
#include "editor_int.h"

// ------------------------------------------------------------------ selection and nodes
static Sel sel_of_node(const Model *m, const AssemblySpec *s, int node) {
  for (int i = node; i > 0; i = m->nodes[i].parent) {
    const Node *n = &m->nodes[i];
    if (n->part_index >= 0) return (Sel){SEL_PART, n->layer_index, n->part_index};
    if (n->wire >= 0) return (Sel){SEL_WIRE, -1, m->wires[n->wire].spec};
    if (n->flags & NF_FLOW) {
      for (int li = 0; li < arr_len(s->layers); li++)
        for (int k = 0; k < arr_len(s->layers[li].links); k++)
          if (s->layers[li].links[k].name && !strcmp(s->layers[li].links[k].name, n->name)) return (Sel){SEL_LINK, li, k};
    }
    if (n->flags & NF_LAYER) return (Sel){SEL_LAYER, n->layer_index, -1};
  }
  return (Sel){SEL_DEVICE, -1, -1};
}

static int node_of_sel(const Model *m, const AssemblySpec *s, Sel sel) {
  for (int i = 1; i < arr_len(m->nodes); i++) {
    const Node *n = &m->nodes[i];
    switch (sel.kind) {
    case SEL_PART:
      if ((n->flags & NF_PART) && n->layer_index == sel.layer && n->part_index == sel.index) return i;
      break;
    case SEL_LAYER:
      if ((n->flags & NF_LAYER) && n->layer_index == sel.layer) return i;
      break;
    case SEL_WIRE:
      if (n->wire >= 0 && m->wires[n->wire].spec == sel.index) return i;
      break;
    case SEL_LINK:
      if ((n->flags & NF_FLOW) && s->layers[sel.layer].links[sel.index].name && !strcmp(n->name, s->layers[sel.layer].links[sel.index].name)) return i;
      break;
    default: return -1;
    }
  }
  return -1;
}

static bool sel_equal(Sel a, Sel b) { return a.kind == b.kind && a.layer == b.layer && a.index == b.index; }

// ------------------------------------------------------------------ names travel
static const char *ren(AssemblySpec *s, const char *cur, const char *from, const char *to) {
  return cur && !strcmp(cur, from) ? spec_str(s, to) : cur;
}

void ed_rename_everywhere(Editor *e, const char *from, const char *to) {
  Doc *d = ed_doc(e);
  AssemblySpec *s = d->spec;
  for (int i = 0; i < arr_len(s->wires); i++) {
    s->wires[i].from = ren(s, s->wires[i].from, from, to);
    s->wires[i].to = ren(s, s->wires[i].to, from, to);
  }
  for (int i = 0; i < arr_len(s->layers); i++)
    for (int k = 0; k < arr_len(s->layers[i].links); k++) {
      LinkSpec *n = &s->layers[i].links[k];
      n->from = ren(s, n->from, from, to);
      for (int q = 0; q < arr_len(n->to); q++) n->to[q] = ren(s, n->to[q], from, to);
    }
  // A video open on this device follows the name too.
  Shot *sh = e->a.shot;
  if (!sh) return;
  for (int i = 0; i < arr_len(sh->scenes); i++) {
    Arena ar = {0};
    bool same = d->path[0] && path_same(path_join(&ar, sh->path, sh->scenes[i].file), d->path);
    arena_free(&ar);
    if (!same) continue;
    for (int k = 0; k < arr_len(sh->tracks); k++)
      if (sh->tracks[k].scene == i && sh->tracks[k].name && !strcmp(sh->tracks[k].name, from) && sh->tracks[k].kind != TRACK_CONTROL) sh->tracks[k].name = shot_str(sh, to);
    for (int k = 0; k < arr_len(sh->labels); k++)
      if (sh->labels[k].scene == i && sh->labels[k].part && !strcmp(sh->labels[k].part, from)) sh->labels[k].part = shot_str(sh, to);
    e->a.dirty = true;
  }
}

// ------------------------------------------------------------------ the tree
static void select_item(Editor *e, Sel s) {
  Doc *d = ed_doc(e);
  d->sel = s;
  e->pick = PICK_NONE;
  ui_unfocus();
}

// A selection made in the 3D view brings its row into sight.
static void reveal(Editor *e, Scroll *sc, float y, float h) {
  if (!e->reveal) return;
  e->reveal = false;
  if (y < sc->r.y) *sc->pos -= sc->r.y - y;
  else if (y + h > sc->r.y + sc->r.h) *sc->pos += y + h - (sc->r.y + sc->r.h);
}

static Scroll *g_tree_scroll;

static bool row(Editor *e, float *y, float x, float w, int depth, const char *label, const char *id, bool on, u32 mark) {
  Rect r = rect(x, *y, w, S(22));
  if (on) reveal(e, g_tree_scroll, *y, S(22));
  char lbl[300];
  snprintf(lbl, sizeof lbl, "%s##%s", label, id);
  bool hit = ui_button(rect(r.x + S(10) * (float)depth, r.y, r.w - S(10) * (float)depth, r.h), lbl, BTN_LEFT | BTN_QUIET | (on ? BTN_ON : 0));
  if (mark) ui_rect(rect(r.x + S(10) * (float)depth + S(1), r.y + S(7), S(3), S(8)), mark);
  *y += S(22);
  return hit;
}

static float heading(float x, float y, const char *text) {
  ui_text_sp(x + S(4), y + S(8), S(10), FONT_REGULAR, C_FAINT, S(1.6f), text);
  return y + S(26);
}

static const char *part_label(const PartSpec *p, char *buf, size_t n) {
  if (p->repeat.on && is_set(p->repeat.count)) snprintf(buf, n, "%s \xC3\x97%d", p->name, (int)p->repeat.count);
  else snprintf(buf, n, "%s", p->name);
  return buf;
}

static void tree(Editor *e, Rect r) {
  Doc *d = ed_doc(e);
  AssemblySpec *s = d->spec;
  Scroll sc = ui_scroll_begin(r, "tree");
  g_tree_scroll = &sc;
  float x = r.x + S(4), w = r.w - S(8), y = sc.y + S(4);
  char id[64], buf[300];
  if (e->tour) {
    y = heading(x, y, "TOUR");
    const Json *t = e->tour->root;
    if (row(e, &y, x, w, 0, json_str(t, "name", "Tour"), "tour", d->sel.kind == SEL_TOUR, 0)) select_item(e, (Sel){SEL_TOUR, -1, -1});
    const Json *lv = json_get(t, "levels");
    int i = 0;
    for (const Json *l = lv ? lv->head : NULL; l; l = l->next, i++) {
      snprintf(id, sizeof id, "lvl%d", i);
      snprintf(buf, sizeof buf, "%d  %s", i + 1, json_str(l, "title", json_str(l, "file", "")));
      if (row(e, &y, x, w, 1, buf, id, d->sel.kind == SEL_LEVEL && d->sel.index == i, 0)) {
        char err[300] = "";
        const char *f = ed_tour_level_file(e, i);
        if (f && ed_open_file(e, f, err, sizeof err) >= 0) {
          ed_doc(e)->sel = (Sel){SEL_LEVEL, -1, i};
          e->part_drag = false;
          ui_scroll_end(&sc, y);
          return;
        }
        if (err[0]) ed_status(e, true, "%s", err);
      }
    }
    y += S(6);
  }
  y = heading(x, y, "DEVICE");
  if (row(e, &y, x, w, 0, s->name, "device", d->sel.kind == SEL_DEVICE, 0)) select_item(e, (Sel){SEL_DEVICE, -1, -1});
  y += S(6);
  y = heading(x, y, "LAYERS, BOTTOM TO TOP");
  for (int li = 0; li < arr_len(s->layers); li++) {
    LayerSpec *l = &s->layers[li];
    bool closed = li < COUNT(d->closed_layer) && d->closed_layer[li];
    snprintf(id, sizeof id, "L%d", li);
    Rect fold = rect(x, y, S(18), S(22));
    snprintf(buf, sizeof buf, "%s##fold%d", closed ? "+" : "\xE2\x88\x92", li);
    if (arr_len(l->parts) + arr_len(l->links) > 0 && ui_button(fold, buf, BTN_QUIET) && li < COUNT(d->closed_layer)) d->closed_layer[li] = !closed;
    Rect rr = rect(x + S(18), y, w - S(18), S(22));
    if (d->sel.kind == SEL_LAYER && d->sel.layer == li) reveal(e, &sc, y, S(22));
    // A part chosen in the view can't stay hidden inside a folded layer.
    if (e->reveal && d->sel.kind >= SEL_PART && d->sel.kind <= SEL_LINK && d->sel.layer == li) closed = false, d->closed_layer[li] = false;
    snprintf(buf, sizeof buf, "%s##%s", l->name, id);
    if (ui_button(rr, buf, BTN_LEFT | BTN_QUIET | (d->sel.kind == SEL_LAYER && d->sel.layer == li ? BTN_ON : 0))) select_item(e, (Sel){SEL_LAYER, li, -1});
    y += S(22);
    if (closed) continue;
    for (int k = 0; k < arr_len(l->parts); k++) {
      snprintf(id, sizeof id, "P%d.%d", li, k);
      if (row(e, &y, x, w, 2, part_label(&l->parts[k], buf, sizeof buf), id, d->sel.kind == SEL_PART && d->sel.layer == li && d->sel.index == k, 0)) select_item(e, (Sel){SEL_PART, li, k});
    }
    for (int k = 0; k < arr_len(l->links); k++) {
      snprintf(id, sizeof id, "N%d.%d", li, k);
      snprintf(buf, sizeof buf, "\xE2\x87\xA2 %s", l->links[k].name ? l->links[k].name : "link");
      if (row(e, &y, x, w, 2, buf, id, d->sel.kind == SEL_LINK && d->sel.layer == li && d->sel.index == k, 0)) select_item(e, (Sel){SEL_LINK, li, k});
    }
  }
  if (arr_len(s->wires)) {
    y += S(6);
    y = heading(x, y, "WIRES");
    for (int i = 0; i < arr_len(s->wires); i++) {
      snprintf(id, sizeof id, "W%d", i);
      const WireKind *wk = wire_kind(s->wires[i].kind);
      Vec3 c = hex_srgb(wk->glow);
      if (row(e, &y, x, w, 0, s->wires[i].name ? s->wires[i].name : "wire", id, d->sel.kind == SEL_WIRE && d->sel.index == i, RGBA((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), 255))) select_item(e, (Sel){SEL_WIRE, -1, i});
    }
  }
  if (arr_len(s->controls)) {
    y += S(6);
    y = heading(x, y, "CONTROLS");
    for (int i = 0; i < arr_len(s->controls); i++) {
      snprintf(id, sizeof id, "C%d", i);
      const ControlSpec *c = &s->controls[i];
      if (row(e, &y, x, w, 0, c->label ? c->label : (c->id ? c->id : "control"), id, d->sel.kind == SEL_CONTROL && d->sel.index == i, 0)) select_item(e, (Sel){SEL_CONTROL, -1, i});
    }
  }
  if (arr_len(s->readouts)) {
    y += S(6);
    y = heading(x, y, "READOUTS");
    for (int i = 0; i < arr_len(s->readouts); i++) {
      snprintf(id, sizeof id, "R%d", i);
      if (row(e, &y, x, w, 0, s->readouts[i].label ? s->readouts[i].label : "readout", id, d->sel.kind == SEL_READOUT && d->sel.index == i, 0)) select_item(e, (Sel){SEL_READOUT, -1, i});
    }
  }
  y += S(10);
  ui_scroll_end(&sc, y);
}

// ------------------------------------------------------------------ adding and taking away
static int active_layer(Doc *d) {
  if (d->sel.kind == SEL_PART || d->sel.kind == SEL_LAYER || d->sel.kind == SEL_LINK) return d->sel.layer;
  return arr_len(d->spec->layers) - 1;
}

// A new thing lands where it can be seen: beside what is selected, on the footprint.
static const char *first_name(const AssemblySpec *s, int nth) {
  int seen = 0;
  for (int i = 0; i < arr_len(s->layers); i++)
    for (int k = 0; k < arr_len(s->layers[i].parts); k++)
      if (seen++ == nth) return s->layers[i].parts[k].name;
  return NULL;
}

static void add_part(Editor *e) {
  Doc *d = ed_doc(e);
  int li = active_layer(d);
  if (li < 0) li = spec_add_layer(d->spec, -1, NULL);
  PartSpec *p = spec_add_part(d->spec, li, (Shape)e->add_shape);
  (void)p;
  d->sel = (Sel){SEL_PART, li, arr_len(d->spec->layers[li].parts) - 1};
  ed_doc_changed(e);
  ed_status(e, false, "Added %s", d->spec->layers[li].parts[d->sel.index].name);
}

static void add_layer(Editor *e) {
  Doc *d = ed_doc(e);
  int at = d->sel.kind == SEL_LAYER || d->sel.kind == SEL_PART || d->sel.kind == SEL_LINK ? d->sel.layer + 1 : arr_len(d->spec->layers);
  int li = spec_add_layer(d->spec, at, NULL);
  d->sel = (Sel){SEL_LAYER, li, -1};
  ed_doc_changed(e);
}

static void add_wire(Editor *e) {
  Doc *d = ed_doc(e);
  const AssemblySpec *s = d->spec;
  const char *a = first_name(s, 0), *b = first_name(s, 1);
  if (d->sel.kind == SEL_PART) a = s->layers[d->sel.layer].parts[d->sel.index].name;
  if (!a || !b) {
    ed_status(e, true, "A wire needs two parts to run between");
    return;
  }
  if (!strcmp(a, b)) b = first_name(s, 0);
  spec_add_wire(d->spec, a, b);
  d->sel = (Sel){SEL_WIRE, -1, arr_len(d->spec->wires) - 1};
  ed_doc_changed(e);
}

static void add_link(Editor *e) {
  Doc *d = ed_doc(e);
  int li = active_layer(d);
  if (li < 0 || arr_len(d->spec->layers[li].parts) < 2) {
    ed_status(e, true, "A link runs between two parts on one layer: this one has fewer");
    return;
  }
  LayerSpec *l = &d->spec->layers[li];
  LinkSpec n = {0};
  char name[64];
  snprintf(name, sizeof name, "Link %d", arr_len(l->links) + 1);
  n.name = spec_str(d->spec, name);
  n.from = l->parts[0].name;
  arr_push(n.to, l->parts[1].name);
  n.lanes = n.width = UNSET;
  l->has_links = true;
  arr_push(l->links, n);
  d->sel = (Sel){SEL_LINK, li, arr_len(l->links) - 1};
  ed_doc_changed(e);
}

static void add_control(Editor *e) {
  Doc *d = ed_doc(e);
  AssemblySpec *s = d->spec;
  char id[32], label[32];
  int n = arr_len(s->controls) + 1;
  snprintf(id, sizeof id, "control%d", n);
  snprintf(label, sizeof label, "Control %d", n);
  ControlSpec c = {0};
  c.id = spec_str(s, id);
  c.label = spec_str(s, label);
  c.unit = spec_str(s, "%");
  c.min = 0, c.max = 100, c.step = 1, c.value = 0;
  s->has_controls = true;
  arr_push(s->controls, c);
  d->sel = (Sel){SEL_CONTROL, -1, n - 1};
  ed_doc_changed(e);
}

static void add_readout(Editor *e) {
  Doc *d = ed_doc(e);
  AssemblySpec *s = d->spec;
  if (!arr_len(s->controls)) {
    ed_status(e, true, "A readout follows a control: add a control first");
    return;
  }
  ReadoutSpec r = {0};
  r.label = spec_str(s, "Readout");
  r.by = s->controls[0].id;
  arr_push(r.map, ((Dbl2){0, 0}));
  arr_push(r.map, ((Dbl2){100, 100}));
  r.digits = 0;
  s->has_readouts = true;
  arr_push(s->readouts, r);
  d->sel = (Sel){SEL_READOUT, -1, arr_len(s->readouts) - 1};
  ed_doc_changed(e);
}

static bool can_dup(const Doc *d) { return d->sel.kind == SEL_PART || d->sel.kind == SEL_LAYER || d->sel.kind == SEL_WIRE; }
static bool can_del(const Doc *d) { return d->sel.kind >= SEL_LAYER && d->sel.kind <= SEL_READOUT; }

static void duplicate(Editor *e) {
  Doc *d = ed_doc(e);
  switch (d->sel.kind) {
  case SEL_PART: {
    PartSpec *c = spec_dup_part(d->spec, d->sel.layer, d->sel.index);
    // Beside the original, so it visibly is a second one.
    double step = fmax(c->has_size ? c->size.x : (is_set(c->radius) ? c->radius * 2 : 5), 1) * 1.2;
    c->at.x += step;
    c->has_at = true;
    d->sel.index++;
    break;
  }
  case SEL_LAYER: d->sel.layer = spec_dup_layer(d->spec, d->sel.layer); break;
  case SEL_WIRE:
    spec_dup_wire(d->spec, d->sel.index);
    d->sel.index++;
    break;
  default: return;
  }
  ed_doc_changed(e);
}

static void delete_sel(Editor *e) {
  Doc *d = ed_doc(e);
  AssemblySpec *s = d->spec;
  switch (d->sel.kind) {
  case SEL_PART:
    spec_remove_part(s, d->sel.layer, d->sel.index);
    d->sel = (Sel){SEL_LAYER, d->sel.layer, -1};
    break;
  case SEL_LINK:
    spec_remove_link(s, d->sel.layer, d->sel.index);
    d->sel = (Sel){SEL_LAYER, d->sel.layer, -1};
    break;
  case SEL_LAYER:
    spec_remove_layer(s, d->sel.layer);
    d->sel = (Sel){SEL_DEVICE, -1, -1};
    break;
  case SEL_WIRE:
    spec_remove_wire(s, d->sel.index);
    d->sel = (Sel){SEL_DEVICE, -1, -1};
    break;
  case SEL_CONTROL:
    arr_remove(s->controls, d->sel.index);
    d->sel = (Sel){SEL_DEVICE, -1, -1};
    break;
  case SEL_READOUT:
    spec_remove_readout(s, d->sel.index);
    d->sel = (Sel){SEL_DEVICE, -1, -1};
    break;
  default: return;
  }
  ed_doc_changed(e);
}

#define SWAP(T, a, b) do { T t_ = (a); (a) = (b); (b) = t_; } while (0)

static void move_sel(Editor *e, int dir) {
  Doc *d = ed_doc(e);
  AssemblySpec *s = d->spec;
  int i = d->sel.kind == SEL_LAYER ? d->sel.layer : d->sel.index, j = i + dir;
  switch (d->sel.kind) {
  case SEL_LAYER:
    if (j < 0 || j >= arr_len(s->layers)) return;
    SWAP(LayerSpec, s->layers[i], s->layers[j]);
    d->sel.layer = j;
    break;
  case SEL_PART: {
    LayerSpec *l = &s->layers[d->sel.layer];
    if (j < 0 || j >= arr_len(l->parts)) return;
    SWAP(PartSpec, l->parts[i], l->parts[j]);
    d->sel.index = j;
    break;
  }
  case SEL_WIRE:
    if (j < 0 || j >= arr_len(s->wires)) return;
    SWAP(WireSpec, s->wires[i], s->wires[j]);
    d->sel.index = j;
    break;
  case SEL_CONTROL:
    if (j < 0 || j >= arr_len(s->controls)) return;
    SWAP(ControlSpec, s->controls[i], s->controls[j]);
    d->sel.index = j;
    break;
  case SEL_READOUT:
    if (j < 0 || j >= arr_len(s->readouts)) return;
    SWAP(ReadoutSpec, s->readouts[i], s->readouts[j]);
    d->sel.index = j;
    break;
  default: return;
  }
  ed_doc_changed(e);
}

static void tree_tools(Editor *e, Rect r) {
  Doc *d = ed_doc(e);
  float pad = S(6), x = r.x + pad, w = r.w - 2 * pad, y = r.y + pad, h = S(24), gap = S(4);
  ui_rect(rect(r.x, r.y, r.w, maxf(1, S(1))), C_LINE);
  float half = (w - gap) / 2, third = (w - 2 * gap) / 3, quarter = (w - 3 * gap) / 4;
  static const char *const SHAPES[] = {"Box", "Cylinder", "Sphere", "Fan", "Port", "Outline"};
  ui_dropdown(rect(x, y, half, h), "add-shape", &e->add_shape, SHAPES, COUNT(SHAPES));
  if (ui_button(rect(x + half + gap, y, half, h), "+ PART", BTN_ON)) add_part(e);
  y += h + gap;
  if (ui_button(rect(x, y, third, h), "+ LAYER", 0)) add_layer(e);
  if (ui_button(rect(x + third + gap, y, third, h), "+ WIRE", 0)) add_wire(e);
  if (ui_button(rect(x + 2 * (third + gap), y, third, h), "+ LINK", 0)) add_link(e);
  y += h + gap;
  if (ui_button(rect(x, y, half, h), "+ CONTROL", 0)) add_control(e);
  if (ui_button(rect(x + half + gap, y, half, h), "+ READOUT", 0)) add_readout(e);
  y += h + gap;
  bool has = can_del(d);
  if (ui_button(rect(x, y, quarter, h), "DUP", can_dup(d) ? 0 : BTN_DISABLED)) duplicate(e);
  if (ui_button(rect(x + quarter + gap, y, quarter, h), "DEL", has ? 0 : BTN_DISABLED)) delete_sel(e);
  if (ui_button(rect(x + 2 * (quarter + gap), y, quarter, h), "UP##move", has && d->sel.kind != SEL_LINK ? 0 : BTN_DISABLED)) move_sel(e, -1);
  if (ui_button(rect(x + 3 * (quarter + gap), y, quarter, h), "DOWN##move", has && d->sel.kind != SEL_LINK ? 0 : BTN_DISABLED)) move_sel(e, 1);
}

// ------------------------------------------------------------------ the 3D view
static void frame_sel(Editor *e, float aspect) {
  Doc *d = ed_doc(e);
  int n = node_of_sel(d->model, d->built, d->sel);
  if (n < 0) {
    stage_frame(&d->stage, d->model, aspect, 1);
    return;
  }
  Vec3 pos, target;
  stage_focus_pose(&d->stage, node_box_now(d->model, n), aspect, &pos, &target);
  stage_fly(&d->stage, pos, target, 0.5f);
}

static void ray_at(const Doc *d, Rect view, const UiInput *in, Vec3 *o, Vec3 *dir) {
  camera_ray(&d->stage.cam, view.w / maxf(view.h, 1), in->mx - view.x, in->my - view.y, view.w, view.h, o, dir);
}

static bool ray_plane_y(Vec3 o, Vec3 dir, float y, Vec3 *out) {
  if (fabsf(dir.y) < 1e-4f) return false;
  float t = (y - o.y) / dir.y;
  if (t <= 0) return false;
  *out = v3mad(o, dir, t);
  return true;
}

// Snaps to round millimetres fit for the device's size: 0.1, 0.2, 0.5, 1, 2...
static double snap_step(const AssemblySpec *s) {
  double span = fmax(s->footprint.x, s->footprint.y) / 200;
  if (span <= 0) return 0.1;
  double p = pow(10, floor(log10(span))), m = span / p;
  return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}

static void commit_part_drag(Editor *e) {
  Doc *d = ed_doc(e);
  e->part_drag = false;
  if (d->sel.kind != SEL_PART) return;
  if (fabsf(e->drag_delta.x) < 1e-6f && fabsf(e->drag_delta.z) < 1e-6f) return;
  PartSpec *p = &d->spec->layers[d->sel.layer].parts[d->sel.index];
  p->at.x = round((p->at.x + (double)e->drag_delta.x) * 1000) / 1000;
  p->at.y = round((p->at.y + (double)e->drag_delta.z) * 1000) / 1000;
  p->has_at = true;
  // Now, not on the timer: the preview's offset dies with this model, and the part must not blink home.
  hist_changed(&d->hist, e->sel_before);
  d->dirty = true;
  d->version++;
  ed_doc_rebuild(e, d);
}

static void view_input(Editor *e, Rect view) {
  Doc *d = ed_doc(e);
  const UiInput *in = e->in;
  Model *m = d->model;
  ui_mark("view", view);
  bool over = rect_has(view, in->mx, in->my) && !ui_wants_mouse() && e->modal == MODAL_NONE;
  UiInput si = *in;
  if (over && in->pressed[0] && !in->shift && e->pick == PICK_NONE && d->sel.kind == SEL_PART && !e->part_drag) {
    Vec3 o, dir;
    ray_at(d, view, in, &o, &dir);
    Hit h = model_pick(m, o, dir);
    if (h.node >= 0 && sel_equal(sel_of_node(m, d->built, h.node), d->sel)) {
      e->part_drag = true;
      e->drag_node = node_of_sel(m, d->built, d->sel);
      e->drag_plane = h.point.y;
      e->drag_p0 = h.point;
      e->drag_delta = v3(0, 0, 0);
      si.pressed[0] = false;
    }
  }
  if (e->part_drag) {
    Vec3 o, dir, p;
    ray_at(d, view, in, &o, &dir);
    if (ray_plane_y(o, dir, e->drag_plane, &p)) {
      float dx = p.x - e->drag_p0.x, dz = p.z - e->drag_p0.z;
      if (!in->shift) {
        float st = (float)snap_step(d->spec);
        dx = roundf(dx / st) * st;
        dz = roundf(dz / st) * st;
      }
      e->drag_delta = v3(dx, 0, dz);
      if (e->drag_node >= 0) m->nodes[e->drag_node].off_anim = e->drag_delta;
    }
    if (!in->down[0]) commit_part_drag(e);
    m = d->model;
  } else {
    if (over && si.pressed[0]) e->orbit_pending = true;
    stage_input(&d->stage, &si, view);
    if (in->released[0] && e->orbit_pending) {
      e->orbit_pending = false;
      if (d->stage.moved < S(6) && over) {
        Vec3 o, dir;
        ray_at(d, view, in, &o, &dir);
        Hit h = model_pick(m, o, dir);
        if (e->pick != PICK_NONE) {
          if (h.node >= 0) props_take_pick(e, m, h);
        } else {
          select_item(e, h.node >= 0 ? sel_of_node(m, d->built, h.node) : (Sel){SEL_DEVICE, -1, -1});
          e->reveal = true;
        }
      }
    }
  }
  e->hover_node = e->hover_copy = -1;
  if (over && !d->stage.dragging && !e->part_drag) {
    Vec3 o, dir;
    ray_at(d, view, in, &o, &dir);
    Hit h = model_pick(m, o, dir);
    e->hover_node = h.node;
    e->hover_copy = h.copy;
  }
}

static void view_tools(Editor *e, Rect view) {
  Doc *d = ed_doc(e);
  float h = S(26), w = minf(S(300), view.w - S(24)), x = view.x + S(12), y = view.y + view.h - h - S(12);
  Rect bar = rect(x, y, w, h);
  ui_rect(bar, RGBA(6, 12, 8, 230));
  ui_border(bar, C_LINE, maxf(1, S(1)));
  ui_block(bar);
  if (ui_button(rect(x, y, S(62), h), "FRAME", BTN_QUIET)) frame_sel(e, view.w / maxf(view.h, 1));
  ui_text(x + S(70), y + S(5), S(11), FONT_REGULAR, C_DIM, "Apart");
  if (ui_slider(rect(x + S(110), y, w - S(116), h), "apart", &d->explode, 0, 1)) model_set_explode(d->model, d->explode);
  // The author's own sliders, to try what they drive. Nothing here is saved.
  const AssemblySpec *s = d->built;
  int nc = arr_len(s->controls), nr = arr_len(s->readouts);
  if (!nc && !nr) return;
  float pw = S(220), ph = S(10) + (float)nc * S(40) + (float)nr * S(20) + S(4), px = view.x + view.w - pw - S(12), py = view.y + view.h - ph - S(12);
  Rect pr = rect(px, py, pw, ph);
  ui_rect(pr, RGBA(6, 12, 8, 230));
  ui_border(pr, C_LINE, maxf(1, S(1)));
  ui_block(pr);
  float cy = py + S(6);
  for (int i = 0; i < nc; i++) {
    const ControlSpec *c = &s->controls[i];
    ui_push_id(i);
    float v = (float)model_get_value(d->model, c->id);
    char txt[96];
    format_readout(txt, sizeof txt, (double)v, c->unit, 0);
    ui_text_fit(px + S(10), cy, S(120), S(11), FONT_REGULAR, C_DIM, c->label ? c->label : c->id);
    ui_text(px + pw - S(10) - ui_text_width(S(11), FONT_REGULAR, 0, txt), cy, S(11), FONT_REGULAR, C_TEXT, txt);
    if (ui_slider(rect(px + S(2), cy + S(14), pw - S(4), S(22)), "try", &v, (float)c->min, (float)c->max)) model_set_value(d->model, c->id, (double)v);
    ui_pop_id();
    cy += S(40);
  }
  for (int i = 0; i < nr; i++) {
    const ReadoutSpec *r = &s->readouts[i];
    double x2 = 0;
    for (int k = 0; k < arr_len(d->model->values); k++)
      if (r->by && !strcmp(d->model->values[k].id, r->by)) x2 = d->model->values[k].value;
    char txt[96];
    format_readout(txt, sizeof txt, lookup(r->map, x2), r->unit, is_set(r->digits) ? (int)r->digits : 0);
    ui_text_fit(px + S(10), cy, S(110), S(11), FONT_REGULAR, C_DIM, r->label ? r->label : "");
    ui_text(px + pw - S(10) - ui_text_width(S(11), FONT_BOLD, 0, txt), cy, S(11), FONT_BOLD, C_BRIGHT, txt);
    cy += S(20);
  }
}

static void draw_view(Editor *e, Rect view) {
  Doc *d = ed_doc(e);
  const UiInput *in = e->in;
  Model *m = d->model;
  float dt = minf(in->dt, 0.1f), aspect = view.w / maxf(view.h, 1);
  stage_tick(&d->stage, dt, (int)view.h);
  model_tick(m, dt);
  model_update(m);
  PostParams post = post_defaults();
  post.time = (float)fmod(e->clock, 100);
  post.grain = 0; // while building, what matters is seeing it clearly
  // What is selected keeps a faint glow, or a small part inside a big device is hard to find again.
  int sel_n = node_of_sel(m, d->built, d->sel);
  SceneHints hints = {e->hover_node >= 0 ? e->hover_node : sel_n, e->hover_node >= 0 ? e->hover_copy : -1};
  ed_view_prepare(e, (int)view.w, (int)view.h);
  r_render(m, &d->stage.cam, &post, &hints, e->vfbo);
  ed_view_blit(e, view);

  // Brackets over the picture.
  ui_clip_push(view);
  ui_set_origin(view.x, view.y);
  if (e->hover_node >= 0) draw_bracket(&d->stage.cam, aspect, node_copy_box_now(m, e->hover_node, e->hover_copy), view.w, view.h, RGBA(255, 255, 255, 89), maxf(1, S(1)));
  int sn = node_of_sel(m, d->built, d->sel);
  if (sn >= 0) draw_bracket(&d->stage.cam, aspect, node_box_now(m, sn), view.w, view.h, c_alpha(C_BRIGHT, 0.95f), maxf(1, S(1.2f)));
  ui_set_origin(0, 0);
  ui_clip_pop();
  if (e->part_drag && d->sel.kind == SEL_PART) {
    const PartSpec *p = &d->spec->layers[d->sel.layer].parts[d->sel.index];
    char tip[96];
    snprintf(tip, sizeof tip, "x %.4g   z %.4g mm", p->at.x + (double)e->drag_delta.x, p->at.y + (double)e->drag_delta.z);
    ui_rect(rect(in->mx + S(14), in->my + S(14), ui_text_width(S(11), FONT_REGULAR, 0, tip) + S(14), S(20)), RGBA(4, 10, 6, 230));
    ui_text(in->mx + S(21), in->my + S(17), S(11), FONT_REGULAR, C_BRIGHT, tip);
  } else if (e->hover_node >= 0 && !d->stage.dragging) {
    const Node *n = &m->nodes[e->hover_node];
    char name[160];
    if (e->hover_copy >= 0 && n->names && e->hover_copy < arr_len(n->names)) snprintf(name, sizeof name, "%s", n->names[e->hover_copy]);
    else snprintf(name, sizeof name, "%s", n->name);
    float tw = ui_text_width(S(11), FONT_REGULAR, 0, name) + S(14);
    float tx = minf(in->mx + S(14), view.x + view.w - tw - S(4));
    ui_rect(rect(tx, in->my + S(14), tw, S(20)), RGBA(4, 10, 6, 230));
    ui_text(tx + S(7), in->my + S(17), S(11), FONT_REGULAR, C_TEXT, name);
  }
  if (e->pick != PICK_NONE) {
    const char *msg = "Click a part in the view";
    float tw = ui_text_width(S(12), FONT_BOLD, 0, msg) + S(24);
    Rect b = rect(view.x + (view.w - tw) / 2, view.y + S(12), tw, S(26));
    ui_rect(b, RGBA(28, 50, 14, 240));
    ui_border(b, C_ACCENT, maxf(1, S(1)));
    ui_text(b.x + S(12), b.y + S(5), S(12), FONT_BOLD, C_BRIGHT, msg);
  }
  view_tools(e, view);
}

// ------------------------------------------------------------------ the tab
static void panel(Rect r) {
  ui_rect(r, RGBA(7, 12, 9, 255));
  ui_block(r);
}

void build_keys(Editor *e) {
  const UiInput *in = e->in;
  Doc *d = ed_doc(e);
  if (ui_wants_keyboard() || e->modal != MODAL_NONE) return;
  if (in->keys[KEY_DELETE] && can_del(d)) delete_sel(e);
  if (in->ctrl && in->keys[KEY_D] && can_dup(d)) duplicate(e);
  if (in->keys[KEY_ESCAPE]) {
    if (e->pick != PICK_NONE) e->pick = PICK_NONE;
    else d->sel = (Sel){SEL_DEVICE, -1, -1};
  }
  if (!in->ctrl && in->keys[KEY_F]) {
    Rect b = ed_body(e);
    frame_sel(e, (b.w - S(590)) / maxf(b.h, 1));
  }
}

void build_frame(Editor *e) {
  Doc *d = ed_doc(e);
  ed_sel_valid(d);
  Rect b = ed_body(e);
  float lw = S(250), rw = S(340);
  Rect left = rect(b.x, b.y, lw, b.h), right = rect(b.x + b.w - rw, b.y, rw, b.h);
  Rect view = rect(b.x + lw, b.y, maxf(b.w - lw - rw, S(40)), b.h);
  build_keys(e);
  if (d->stale && e->clock - d->built_at >= 0.03 && !e->part_drag) ed_doc_rebuild(e, d);
  if (!d->framed) {
    stage_frame(&d->stage, d->model, view.w / maxf(view.h, 1), 1);
    d->framed = true;
  }
  view_input(e, view);
  draw_view(e, view);
  panel(left);
  ui_rect(rect(left.x + left.w - maxf(1, S(1)), left.y, maxf(1, S(1)), left.h), C_LINE);
  float tools_h = S(122);
  tree(e, rect(left.x, left.y, left.w - maxf(1, S(1)), left.h - tools_h));
  tree_tools(e, rect(left.x, left.y + left.h - tools_h, left.w, tools_h));
  panel(right);
  ui_rect(rect(right.x, right.y, maxf(1, S(1)), right.h), C_LINE);
  props_panel(e, right);
}
