// `3disle check`: every file under the data folder opens, and everything it names exists. It names the
// file and what is wrong.
#include "app.h"
#include "model.h"
#include "shot.h"
#include <dirent.h>
#include <sys/stat.h>

static int g_problems;

static void problem(const char *file, const char *fmt, const char *what) {
  char msg[600];
  snprintf(msg, sizeof msg, fmt, what ? what : "");
  printf("%s: %s\n", file, msg);
  g_problems++;
}

static bool exists(const char *from, const char *rel) {
  Arena ar = {0};
  size_t n;
  char *d = file_read(path_join(&ar, from, rel), &n);
  arena_free(&ar);
  if (!d) return false;
  free(d);
  return true;
}

static bool names_part(const Model *m, const char *name) {
  if (model_find(m, name) >= 0) return true;
  for (int i = 0; i < arr_len(m->nodes); i++) {
    const Node *n = &m->nodes[i];
    for (int k = 0; n->names && k < arr_len(n->names); k++)
      if (n->names[k] && !strcmp(n->names[k], name)) return true;
    // A row answers to its name with a number, as the builder and links write it.
    if (n->name && n->inst) {
      size_t l = strlen(n->name);
      if (!strncmp(n->name, name, l) && name[l] == ' ') return true;
    }
  }
  return false;
}

static bool is_control(const AssemblySpec *s, const char *id) {
  for (int i = 0; id && i < arr_len(s->controls); i++)
    if (s->controls[i].id && !strcmp(s->controls[i].id, id)) return true;
  return false;
}

static void check_rule(const char *file, const AssemblySpec *s, const Value *v) {
  if (v->set && v->is_rule && !is_control(s, v->by)) problem(file, "a rule follows \"%s\", which is no control of this file", v->by);
}

static void check_assembly(const char *file) {
  char err[1024] = "";
  AssemblySpec *s = spec_load(file, err, sizeof err);
  if (!s) return problem(file, "%s", err);
  Model *m = model_build(s, (BuildOpts){.paint = false});
  if (m->file_error) problem(file, "%s", m->file_error);
  for (int i = 0; i < arr_len(m->missing); i++) problem(file, "a wire names \"%s\", which is no part of this model", m->missing[i]);
  for (int i = 0; i < arr_len(s->layers); i++) {
    const LayerSpec *l = &s->layers[i];
    if (l->detail && !exists(file, l->detail)) problem(file, "links to %s, which isn't there", l->detail);
    for (int k = 0; k < arr_len(l->parts); k++) {
      const PartSpec *p = &l->parts[k];
      if (p->detail && !exists(file, p->detail)) problem(file, "links to %s, which isn't there", p->detail);
      check_rule(file, s, &p->spin);
      check_rule(file, s, &p->pulse_level);
      if (p->shape == SHAPE_FILE && p->file && arr_len(p->nodes)) {
        Arena ar = {0};
        ModelFile *f = modelfile_load(path_join(&ar, file, p->file), NULL, 0);
        arena_free(&ar);
        if (f) {
          for (int q = 0; q < arr_len(p->nodes); q++) {
            bool *on = modelfile_pick(f, &p->nodes[q], 1);
            bool any = false;
            for (int o = 0; o < arr_len(f->objects) && !any; o++) any = on[o];
            arr_free(on);
            if (!any) problem(file, "names object \"%s\", which isn't in its model file", p->nodes[q]);
          }
        }
      }
    }
    for (int k = 0; k < arr_len(l->links); k++) {
      const LinkSpec *n = &l->links[k];
      check_rule(file, s, &n->level);
      if (n->from && !names_part(m, n->from)) problem(file, "a link starts at \"%s\", which is no part of this model", n->from);
      for (int q = 0; q < arr_len(n->to); q++)
        if (!names_part(m, n->to[q])) problem(file, "a link goes to \"%s\", which is no part of this model", n->to[q]);
    }
  }
  for (int i = 0; i < arr_len(s->wires); i++) check_rule(file, s, &s->wires[i].level);
  for (int i = 0; i < arr_len(s->readouts); i++)
    if (s->readouts[i].by && !is_control(s, s->readouts[i].by)) problem(file, "a readout follows \"%s\", which is no control of this file", s->readouts[i].by);
  model_free(m);
  spec_free(s);
}

static void check_tour(const char *file, const Json *j) {
  const Json *lv = json_get(j, "levels");
  if (!lv || lv->type != J_ARR || !lv->count) return problem(file, "a tour with no levels%s", "");
  for (const Json *l = lv->head; l; l = l->next) {
    const char *f = json_str(l, "file", "");
    if (!exists(file, f)) {
      problem(file, "level file %s isn't there", f);
      continue;
    }
    const char *into = json_str(l, "into", NULL);
    if (!into) continue;
    Arena ar = {0};
    AssemblySpec *s = spec_load(path_join(&ar, file, f), NULL, 0);
    if (s) {
      Model *m = model_build(s, (BuildOpts){.paint = false});
      if (!names_part(m, into)) problem(file, "a level zooms into \"%s\", which isn't in it", into);
      model_free(m);
      spec_free(s);
    }
    arena_free(&ar);
  }
}

static void check_list(const char *file, const Json *j) {
  for (const Json *e = j->head; e; e = e->next) {
    const char *f = json_str(e, "file", NULL);
    if (f && !exists(file, f)) problem(file, "lists %s, which isn't there", f);
  }
}

static void check_shot(const char *file) {
  char err[1024] = "";
  Shot *s = shot_load(file, err, sizeof err);
  if (!s) return problem(file, "%s", err);
  AssemblySpec **specs = NULL;
  Model **models = NULL;
  for (int i = 0; i < arr_len(s->scenes); i++) {
    Arena ar = {0};
    AssemblySpec *sp = spec_load(path_join(&ar, file, s->scenes[i].file), NULL, 0);
    arena_free(&ar);
    arr_push(specs, sp);
    arr_push(models, sp ? model_build(sp, (BuildOpts){.paint = false}) : NULL);
    if (!sp) problem(file, "scene file %s doesn't open", s->scenes[i].file);
  }
  for (int i = 0; i < arr_len(s->tracks); i++) {
    const Track *t = &s->tracks[i];
    if (t->scene < 0 || t->scene >= arr_len(models) || !models[t->scene] || !t->name) continue;
    if ((t->kind == TRACK_OFFSET || t->kind == TRACK_OPACITY || t->kind == TRACK_GLOW) && !names_part(models[t->scene], t->name)) problem(file, "a track moves \"%s\", which isn't in its scene", t->name);
  }
  for (int i = 0; i < arr_len(s->labels); i++) {
    const ShotLabel *l = &s->labels[i];
    if (l->scene >= 0 && l->scene < arr_len(models) && models[l->scene] && l->part && !names_part(models[l->scene], l->part)) problem(file, "a label names \"%s\", which isn't in its scene", l->part);
  }
  for (int i = 0; i < arr_len(models); i++) {
    if (models[i]) model_free(models[i]);
    spec_free(specs[i]);
  }
  arr_free(models);
  arr_free(specs);
  shot_free(s);
}

static void check_file(const char *path) {
  char *text = file_read(path, NULL);
  if (!text) return problem(path, "can't be read%s", "");
  Arena ar = {0};
  char err[512] = "";
  Json *j = json_parse(&ar, text, err, sizeof err);
  free(text);
  if (!j) {
    problem(path, "%s", err);
  } else if (j->type == J_ARR) {
    check_list(path, j);
  } else if (json_get(j, "levels")) {
    check_tour(path, j);
  } else if (json_get(j, "scenes")) {
    check_shot(path);
  } else if (json_get(j, "layers") || (json_get(j, "model") && json_get(j, "parts"))) {
    check_assembly(path);
  } else if (!json_get(j, "dependencies")) {
    problem(path, "isn't a device, tour, video or list%s", "");
  }
  arena_free(&ar);
}

static int walk(const char *dir) {
  int files = 0;
  DIR *d = opendir(dir);
  if (!d) return problem(dir, "can't be opened%s", ""), 0;
  struct dirent *en;
  while ((en = readdir(d))) {
    if (en->d_name[0] == '.') continue;
    char full[2048];
    snprintf(full, sizeof full, "%s/%s", dir, en->d_name);
    struct stat st;
    if (stat(full, &st)) continue;
    if (S_ISDIR(st.st_mode)) {
      files += walk(full);
    } else if (strstr(en->d_name, ".json")) {
      check_file(full);
      files++;
    }
  }
  closedir(d);
  return files;
}

int cmd_check(int argc, char **argv) {
  const char *root = argc > 0 ? argv[0] : "public";
  struct stat st;
  int files = 1;
  if (!stat(root, &st) && S_ISDIR(st.st_mode)) files = walk(root);
  else check_file(root);
  printf("%d files, %d problems\n", files, g_problems);
  return g_problems ? 1 : 0;
}
