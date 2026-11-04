// Drives the app without a window, one step per argument, the same way a person would: the mouse
// moves and clicks, keys press, time passes. Every frame goes through the real input path, so a
// check here means the button really works. Steps:
//   size:WxH  wait:SECONDS  mouse:X,Y  click:X,Y  rclick:X,Y  drag:X,Y,X2,Y2  wheel:N  key:NAME  type:TEXT
//   shot:FILE.png  explode:A  labels:0|1  select:PARTNAME  step:1|-1  print:WHAT
#include "app.h"
#include "gl.h"
#include "headless.h"
#include "ui.h"
#include "viewer.h"

typedef struct {
  UiInput in;
  int w, h;
  u32 fbo;
  Viewer *v;
} Driver;

static const struct {
  const char *name;
  UiKey key;
} KEYS[] = {{"left", KEY_LEFT}, {"right", KEY_RIGHT}, {"up", KEY_UP},     {"down", KEY_DOWN},   {"enter", KEY_ENTER}, {"escape", KEY_ESCAPE},
            {"backspace", KEY_BACKSPACE}, {"delete", KEY_DELETE}, {"tab", KEY_TAB},  {"space", KEY_SPACE}, {"l", KEY_L}};

static void frame(Driver *d, float dt) {
  d->in.dt = dt;
  d->in.w = d->w;
  d->in.h = d->h;
  viewer_frame(d->v, &d->in, d->fbo);
  memset(d->in.pressed, 0, sizeof d->in.pressed);
  memset(d->in.released, 0, sizeof d->in.released);
  memset(d->in.keys, 0, sizeof d->in.keys);
  d->in.wheel = 0;
  d->in.text = NULL;
  d->in.double_click = false;
}

static void shot(Driver *d, const char *path) {
  u8 *px = malloc((size_t)d->w * (size_t)d->h * 4);
  r_read_rgba(d->fbo, d->w, d->h, px);
  if (write_png(path, d->w, d->h, px)) printf("saved %s\n", path);
  free(px);
}

int cmd_script(int argc, char **argv) {
  if (argc < 1) return fprintf(stderr, "usage: 3disle script <file.json> [steps...]\n"), 2;
  char err[2048] = "";
  if (!headless_init(err, sizeof err) || !r_init(err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  ui_init();
  Driver d = {.w = 1400, .h = 850};
  d.in.scale = 1;
  d.fbo = r_offscreen_fbo(d.w, d.h);
  r_set_size(d.w, d.h, 1.5f, 4);
  const float DT = 1.0f / 60;
  d.v = viewer_new("public", false);
  if (!viewer_open(d.v, argv[0], err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  frame(&d, DT);
  for (int i = 1; i < argc; i++) {
    char *a = argv[i];
    char *arg = strchr(a, ':');
    if (arg) *arg++ = 0;
    else arg = (char *)"";
    if (!strcmp(a, "size")) {
      sscanf(arg, "%dx%d", &d.w, &d.h);
      d.fbo = r_offscreen_fbo(d.w, d.h);
      r_set_size(d.w, d.h, 1.5f, 4);
      frame(&d, DT);
    } else if (!strcmp(a, "scale")) {
      d.in.scale = (float)atof(arg);
    } else if (!strcmp(a, "wait")) {
      int n = (int)ceil(atof(arg) * 60);
      for (int k = 0; k < n; k++) frame(&d, DT);
    } else if (!strcmp(a, "mouse")) {
      sscanf(arg, "%f,%f", &d.in.mx, &d.in.my);
      frame(&d, DT);
      frame(&d, DT);
    } else if (!strcmp(a, "click") || !strcmp(a, "rclick")) {
      int b = !strcmp(a, "click") ? 0 : 1;
      sscanf(arg, "%f,%f", &d.in.mx, &d.in.my);
      frame(&d, DT);
      d.in.down[b] = d.in.pressed[b] = true;
      frame(&d, DT);
      d.in.down[b] = false;
      d.in.released[b] = true;
      frame(&d, DT);
      frame(&d, DT);
    } else if (!strcmp(a, "drag")) {
      float x0, y0, x1, y1;
      sscanf(arg, "%f,%f,%f,%f", &x0, &y0, &x1, &y1);
      d.in.mx = x0, d.in.my = y0;
      frame(&d, DT);
      d.in.down[0] = d.in.pressed[0] = true;
      frame(&d, DT);
      for (int k = 1; k <= 12; k++) {
        d.in.mx = lerpf(x0, x1, (float)k / 12);
        d.in.my = lerpf(y0, y1, (float)k / 12);
        frame(&d, DT);
      }
      d.in.down[0] = false;
      d.in.released[0] = true;
      frame(&d, DT);
    } else if (!strcmp(a, "wheel")) {
      d.in.wheel = (float)atof(arg);
      frame(&d, DT);
    } else if (!strcmp(a, "key")) {
      for (int k = 0; k < COUNT(KEYS); k++)
        if (!strcmp(KEYS[k].name, arg)) d.in.keys[KEYS[k].key] = true;
      frame(&d, DT);
    } else if (!strcmp(a, "type")) {
      d.in.text = arg;
      frame(&d, DT);
    } else if (!strcmp(a, "shot")) {
      frame(&d, DT);
      shot(&d, arg);
    } else if (!strcmp(a, "explode")) {
      viewer_set_explode(d.v, (float)atof(arg));
      frame(&d, DT);
    } else if (!strcmp(a, "labels")) {
      viewer_set_labels(d.v, atoi(arg) != 0);
      frame(&d, DT);
    } else if (!strcmp(a, "select")) {
      viewer_select(d.v, model_find(viewer_model(d.v), arg), -1);
      frame(&d, DT);
    } else if (!strcmp(a, "step")) {
      viewer_step(d.v, atoi(arg));
      frame(&d, DT);
    } else if (!strcmp(a, "print")) {
      printf("%s: title=%s level=%d\n", arg, viewer_title(d.v), viewer_level(d.v));
    } else {
      fprintf(stderr, "unknown step %s\n", a);
      return 2;
    }
  }
  GLenum e = glGetError();
  if (e) printf("GL error 0x%x\n", e);
  viewer_free(d.v);
  headless_shutdown();
  return 0;
}
