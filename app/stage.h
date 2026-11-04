// The camera: orbit with inertia, zoom toward the pointer, glides, and framing a model.
#pragma once
#include "render.h"
#include "ui.h"

typedef struct {
  Camera cam;
  float pend_theta, pend_phi, pend_zoom, pend_px, pend_py;
  Vec3 zoom_at;
  bool flying;
  float fly_t, fly_dur;
  Vec3 from_pos, from_target, to_pos, to_target;
  bool dragging;
  float press_x, press_y;
  float moved; // how far the pointer has moved since it went down
} Stage;

void stage_init(Stage *s);
float stage_aspect(const UiInput *in);
void stage_input(Stage *s, const UiInput *in, Rect view);
bool stage_tick(Stage *s, float dt, float aspect); // true while something moves
void stage_fly(Stage *s, Vec3 pos, Vec3 target, float seconds);
// The three-quarter view that fits the model. `from` scales the starting distance: 5 starts far
// away and glides in, 0.25 starts close and backs away; 1 lands there at once.
void stage_frame(Stage *s, const Model *m, float aspect, float from);
// Where to look at a box from the direction the camera already looks from.
void stage_focus_pose(const Stage *s, Box b, float aspect, Vec3 *pos, Vec3 *target);
Vec3 stage_view_dir(void);
float fit_distance(float radius, float fov_deg, float aspect);
