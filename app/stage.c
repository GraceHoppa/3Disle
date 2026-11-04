#include "stage.h"

Vec3 stage_view_dir(void) { return v3norm(v3(1, 0.7f, 1.2f)); }

void stage_init(Stage *s) {
  memset(s, 0, sizeof *s);
  s->cam.fov_deg = 40;
  s->cam.pos = v3(0, 0, 100);
  s->cam.near_z = 0.1f;
  s->cam.far_z = 1000;
}

float stage_aspect(const UiInput *in) { return (float)in->w / (float)maxi(in->h, 1); }

float fit_distance(float radius, float fov_deg, float aspect) {
  float vfov = fov_deg * PI / 180;
  float hfov = 2 * atanf(tanf(vfov / 2) * aspect);
  return radius / sinf(minf(vfov, hfov) / 2);
}

void stage_fly(Stage *s, Vec3 pos, Vec3 target, float seconds) {
  s->flying = true;
  s->fly_t = 0;
  s->fly_dur = maxf(seconds, 1e-3f);
  s->from_pos = s->cam.pos;
  s->from_target = s->cam.target;
  s->to_pos = pos;
  s->to_target = target;
  s->pend_theta = s->pend_phi = s->pend_zoom = s->pend_px = s->pend_py = 0;
}

void stage_frame(Stage *s, const Model *m, float aspect, float from) {
  float dist = fit_distance(m->radius, s->cam.fov_deg, aspect) * 1.15f;
  Vec3 dir = stage_view_dir();
  s->cam.near_z = m->radius / 100;
  s->cam.far_z = m->radius * 100 * maxf(1, from);
  s->cam.target = m->center;
  s->cam.pos = v3mad(m->center, dir, dist * from);
  s->flying = false;
  s->pend_theta = s->pend_phi = s->pend_zoom = s->pend_px = s->pend_py = 0;
  if (from != 1) stage_fly(s, v3mad(m->center, dir, dist), m->center, 1.1f);
}

void stage_focus_pose(const Stage *s, Box b, float aspect, Vec3 *pos, Vec3 *target) {
  Vec3 c = box_center(b);
  float r = maxf(box_radius(b), 1e-6f);
  float dist = fit_distance(r, s->cam.fov_deg, aspect) * 1.5f;
  Vec3 dir = v3sub(s->cam.pos, c);
  if (v3dot(dir, dir) < 1e-12f) dir = v3(0, 0.5f, 1);
  *target = c;
  *pos = v3mad(c, v3norm(dir), dist);
}

void stage_input(Stage *s, const UiInput *in, Rect view) {
  bool over = rect_has(view, in->mx, in->my) && !ui_wants_mouse();
  static float last_x, last_y;
  if ((in->pressed[0] || in->pressed[1] || in->pressed[2]) && over) {
    s->dragging = true;
    s->press_x = in->mx;
    s->press_y = in->my;
    s->moved = 0;
    last_x = in->mx;
    last_y = in->my;
  }
  if (s->dragging) {
    float dx = in->mx - last_x, dy = in->my - last_y;
    last_x = in->mx;
    last_y = in->my;
    s->moved = maxf(s->moved, hypotf(in->mx - s->press_x, in->my - s->press_y));
    if (dx != 0 || dy != 0) {
      if (in->down[0] && !in->shift) {
        // A full height's drag turns a full 0.7 revolutions, as the orbit the look was tuned with.
        s->pend_theta -= TAU * dx / (float)in->h * 0.7f;
        s->pend_phi -= TAU * dy / (float)in->h * 0.7f;
        s->flying = false;
      } else if (in->down[1] || in->down[2] || (in->down[0] && in->shift)) {
        s->pend_px += dx;
        s->pend_py += dy;
        s->flying = false;
      }
    }
    if (!in->down[0] && !in->down[1] && !in->down[2]) s->dragging = false;
  }
  if (over && in->wheel != 0) {
    s->pend_zoom -= in->wheel * 0.09f * 0.9f * 1.2f;
    Vec3 o, d;
    camera_ray(&s->cam, (view.w) / maxf(view.h, 1), in->mx - view.x, in->my - view.y, view.w, view.h, &o, &d);
    Vec3 f = v3norm(v3sub(s->cam.target, s->cam.pos));
    float along = v3dist(s->cam.target, s->cam.pos) / maxf(v3dot(d, f), 0.05f);
    s->zoom_at = v3mad(o, d, along);
    s->flying = false;
  }
}

bool stage_tick(Stage *s, float dt, float aspect) {
  bool busy = false;
  if (s->flying) {
    s->fly_t += dt;
    float k = minf(s->fly_t / s->fly_dur, 1);
    float e = k < 0.5f ? 4 * k * k * k : 1 - powf(-2 * k + 2, 3) / 2;
    s->cam.pos = v3lerp(s->from_pos, s->to_pos, e);
    s->cam.target = v3lerp(s->from_target, s->to_target, e);
    if (k >= 1) s->flying = false;
    return true;
  }
  // What the pointer asked for arrives over a few frames, at any frame rate.
  float k = 1 - powf(0.92f, dt * 60);
  float dth = s->pend_theta * k, dph = s->pend_phi * k, dz = s->pend_zoom * k, dpx = s->pend_px * k, dpy = s->pend_py * k;
  s->pend_theta -= dth, s->pend_phi -= dph, s->pend_zoom -= dz, s->pend_px -= dpx, s->pend_py -= dpy;
  if (fabsf(dth) + fabsf(dph) + fabsf(dz) + fabsf(dpx) + fabsf(dpy) > 1e-5f) busy = true;
  Vec3 off = v3sub(s->cam.pos, s->cam.target);
  float dist = v3len(off);
  float th = atan2f(off.x, off.z), ph = acosf(clampf(off.y / maxf(dist, 1e-6f), -1, 1));
  th += dth;
  ph = clampf(ph + dph, 0.001f, PI - 0.001f);
  float f = expf(dz);
  if (dz != 0) s->cam.target = v3lerp(s->cam.target, s->zoom_at, 1 - f);
  dist *= f;
  Vec3 dir = v3(sinf(ph) * sinf(th), cosf(ph), sinf(ph) * cosf(th));
  // Pan in the view's plane, the pointer staying on what it grabbed.
  if (dpx != 0 || dpy != 0) {
    Vec3 fw = v3scale(dir, -1), right = v3norm(v3cross(fw, v3(0, 1, 0))), up = v3cross(right, fw);
    float scale = 2 * dist * tanf(s->cam.fov_deg * PI / 360) / maxf(s->press_y > 0 ? 1 : 1, 1);
    (void)aspect;
    extern int stage_view_height;
    scale /= (float)maxi(stage_view_height, 1);
    s->cam.target = v3add(s->cam.target, v3add(v3scale(right, -dpx * scale), v3scale(up, dpy * scale)));
  }
  s->cam.pos = v3mad(s->cam.target, dir, dist);
  // Keep the depth range around where the camera is.
  return busy;
}
int stage_view_height = 800;
