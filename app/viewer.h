// The viewer: open a device or tour, orbit, take it apart, inspect parts, dive into them. The same
// code is the desktop viewer and the exported web page.
#pragma once
#include "stage.h"

typedef struct Viewer Viewer;

Viewer *viewer_new(const char *data_root, bool embed);
// A device (.json) or tour (.json with "levels"). Returns false with `err` filled.
bool viewer_open(Viewer *v, const char *path, char *err, size_t errlen);
// One frame: reads input, moves things, draws to `fbo`.
void viewer_frame(Viewer *v, const UiInput *in, u32 fbo);
// Show a document that may not be saved yet; `path` is where its links resolve from.
bool viewer_open_doc(Viewer *v, const AssemblySpec *doc, const char *path, float aspect);
// Called each frame just before the interface is drawn, for the host's own widgets.
void viewer_set_overlay(Viewer *v, void (*fn)(void *ud), void *ud);
void viewer_set_intro(Viewer *v, bool intro);
void viewer_set_status(Viewer *v, const char *text);
const char *viewer_title(const Viewer *v);
// True while anything moves; an idle window may wait for input.
bool viewer_busy(const Viewer *v);
// For tests: where things are.
Model *viewer_model(Viewer *v);
Stage *viewer_stage(Viewer *v);
void viewer_set_explode(Viewer *v, float amount);
void viewer_select(Viewer *v, int node, int copy);
void viewer_step(Viewer *v, int dir);
void viewer_set_labels(Viewer *v, bool on);
int viewer_level(const Viewer *v);
void viewer_free(Viewer *v);

// Drawn on top of a 3D view by the viewer and in rendered video alike.
void draw_tag(float x, float y, float lead, const char *text, float alpha);
void draw_bracket(const Camera *cam, float aspect, Box b, float w, float h, u32 color, float thick);
// Big scale number, title, text and caption, rising into place: t is seconds since it changed.
void draw_hud(float x, float bottom, float maxw, const char *scale, const char *title, const char *text, const char *caption, float t, float cap_t, float alpha);
// fmt helpers.
void fmt_length(char *out, size_t n, float mm);
double metres_of(const char *scale);
