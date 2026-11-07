// Drives the app without a window, one step per argument, the same way a person would: the mouse
// moves and clicks, keys press, time passes. Every frame goes through the real input path, so a
// check here means the button really works. Steps:
//   size:WxH  wait:SECONDS  mouse:X,Y  click:X,Y  dclick:X,Y  rclick:X,Y  drag:X,Y,X2,Y2  wheel:N
//   key:NAME  ctrl:NAME  shift:0|1  type:TEXT  shot:FILE.png  print:WHAT
// The viewer also takes explode:A  labels:0|1  select:PARTNAME  step:1|-1
// The app (`--app`) names what it presses instead of giving pixels, as "NAME" or "NAME@FX,FY" for a
// point inside it, and can check itself:
//   press:NAME  dpress:NAME  hover:NAME  rdrag:NAME|DX|DY  dragby:NAME|DX|DY  field:NAME=TEXT  pick:NAME=OPTION
//   expect:WHAT=VALUE  marks  record:FILE.mp4  record:  title:TEXT
#include "app.h"
#include "editor.h"
#include "gl.h"
#include "headless.h"
#include "ui.h"
#include "viewer.h"
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  UiInput in;
  int w, h;
  u32 fbo;
  Viewer *v;
  Editor *e;
  // Recording what happens, with the pointer drawn in.
  int rec_fd;
  pid_t rec_pid;
  u8 *rec_px;
  int rec_w, rec_h;
  int failed;
} Driver;

static const struct {
  const char *name;
  UiKey key;
} KEYS[] = {{"left", KEY_LEFT}, {"right", KEY_RIGHT}, {"up", KEY_UP},     {"down", KEY_DOWN},   {"enter", KEY_ENTER}, {"escape", KEY_ESCAPE},
            {"backspace", KEY_BACKSPACE}, {"delete", KEY_DELETE}, {"tab", KEY_TAB},  {"space", KEY_SPACE}, {"home", KEY_HOME}, {"end", KEY_END},
            {"a", KEY_A}, {"c", KEY_C}, {"d", KEY_D}, {"e", KEY_E}, {"f", KEY_F}, {"g", KEY_G}, {"k", KEY_K}, {"l", KEY_L}, {"n", KEY_N}, {"o", KEY_O},
            {"r", KEY_R}, {"s", KEY_S}, {"v", KEY_V}, {"x", KEY_X}, {"y", KEY_Y}, {"z", KEY_Z}, {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3}};

static void cursor_dot(u8 *px, int w, int h, float cx, float cy) {
  // An arrow: a filled triangle with a dark edge, so it shows on anything.
  static const char *const ARROW[] = {
      "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ", "X.....X     ", "X......X    ",
      "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ", "X.X X..X    ", "XX  X..X    ", "X    X..X   ", "     X..X   ", "      XX    "};
  for (int y = 0; y < COUNT(ARROW); y++)
    for (int x = 0; ARROW[y][x]; x++) {
      char c = ARROW[y][x];
      if (c == ' ') continue;
      int px_x = (int)cx + x, px_y = (int)cy + y;
      if (px_x < 0 || px_y < 0 || px_x >= w || px_y >= h) continue;
      u8 *p = px + ((size_t)px_y * (size_t)w + (size_t)px_x) * 4;
      u8 v = c == 'X' ? 12 : 245;
      p[0] = p[1] = p[2] = v;
      p[3] = 255;
    }
}

static void frame(Driver *d, float dt) {
  d->in.dt = dt;
  d->in.w = d->w;
  d->in.h = d->h;
  if (d->e) editor_frame(d->e, &d->in, d->fbo);
  else viewer_frame(d->v, &d->in, d->fbo);
  memset(d->in.pressed, 0, sizeof d->in.pressed);
  memset(d->in.released, 0, sizeof d->in.released);
  memset(d->in.keys, 0, sizeof d->in.keys);
  d->in.wheel = 0;
  d->in.text = NULL;
  d->in.double_click = false;
  if (d->rec_fd) {
    r_read_rgba(d->fbo, d->w, d->h, d->rec_px);
    cursor_dot(d->rec_px, d->w, d->h, d->in.mx, d->in.my);
    size_t left = (size_t)d->w * (size_t)d->h * 4;
    const u8 *q = d->rec_px;
    while (left) {
      ssize_t n = write(d->rec_fd, q, left);
      if (n <= 0) break;
      q += n;
      left -= (size_t)n;
    }
  }
}

static const float DT = 1.0f / 60;

static void pause_frames(Driver *d, int n) {
  if (!d->rec_fd) return;
  for (int i = 0; i < n; i++) frame(d, DT);
}

// In a recording the pointer travels; otherwise it just arrives.
static void go_to(Driver *d, float x, float y) {
  if (d->rec_fd) {
    float x0 = d->in.mx, y0 = d->in.my;
    int n = (int)clampf(hypotf(x - x0, y - y0) / 24, 6, 26);
    for (int k = 1; k <= n; k++) {
      float t = (float)k / (float)n, s = t * t * (3 - 2 * t);
      d->in.mx = lerpf(x0, x, s);
      d->in.my = lerpf(y0, y, s);
      frame(d, DT);
    }
  } else {
    d->in.mx = x, d->in.my = y;
  }
  frame(d, DT);
}

static void click_at(Driver *d, float x, float y, int b, bool twice) {
  go_to(d, x, y);
  d->in.down[b] = d->in.pressed[b] = true;
  if (twice) d->in.double_click = true;
  frame(d, DT);
  d->in.down[b] = false;
  d->in.released[b] = true;
  frame(d, DT);
  frame(d, DT);
  pause_frames(d, 12);
}

static void drag_path(Driver *d, float x0, float y0, float x1, float y1, int b) {
  go_to(d, x0, y0);
  d->in.down[b] = d->in.pressed[b] = true;
  frame(d, DT);
  int n = d->rec_fd ? 34 : 12;
  for (int k = 1; k <= n; k++) {
    d->in.mx = lerpf(x0, x1, (float)k / (float)n);
    d->in.my = lerpf(y0, y1, (float)k / (float)n);
    frame(d, DT);
  }
  d->in.down[b] = false;
  d->in.released[b] = true;
  frame(d, DT);
  frame(d, DT);
  pause_frames(d, 12);
}

static void shot(Driver *d, const char *path) {
  u8 *px = malloc((size_t)d->w * (size_t)d->h * 4);
  r_read_rgba(d->fbo, d->w, d->h, px);
  if (write_png(path, d->w, d->h, px)) printf("saved %s\n", path);
  free(px);
}

// "NAME" or "NAME@FX,FY": where it was on the last frame. A name that isn't on screen fails the run.
static bool locate(Driver *d, const char *spec, float *x, float *y, Rect *out) {
  char name[200];
  snprintf(name, sizeof name, "%s", spec);
  float fx = 0.5f, fy = 0.5f;
  char *at = strrchr(name, '@');
  if (at) {
    *at = 0;
    sscanf(at + 1, "%f,%f", &fx, &fy);
  }
  Rect r;
  if (!ui_find(name, &r)) {
    frame(d, DT); // it may have appeared this very frame
    if (!ui_find(name, &r)) {
      fprintf(stderr, "FAIL: nothing on screen called \"%s\"\n", name);
      d->failed = 1;
      return false;
    }
  }
  *x = r.x + r.w * fx;
  *y = r.y + r.h * fy;
  if (out) *out = r;
  return true;
}

static void type_text(Driver *d, const char *text) {
  if (!d->rec_fd) {
    d->in.text = text;
    frame(d, DT);
    return;
  }
  for (const char *c = text; *c; c++) {
    char one[2] = {*c, 0};
    d->in.text = one;
    frame(d, DT);
    frame(d, DT);
    frame(d, DT);
  }
}

static bool record_start(Driver *d, const char *path) {
  int fd[2];
  if (pipe(fd)) return false;
  signal(SIGPIPE, SIG_IGN);
  pid_t pid = fork();
  if (pid == 0) {
    dup2(fd[0], 0);
    close(fd[0]);
    close(fd[1]);
    char size[32];
    snprintf(size, sizeof size, "%dx%d", d->w, d->h);
    execlp("ffmpeg", "ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", size, "-r", "60", "-i", "-", "-c:v", "libx264",
           "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart", path, (char *)NULL);
    _exit(127);
  }
  close(fd[0]);
  d->rec_fd = fd[1];
  d->rec_pid = pid;
  d->rec_px = malloc((size_t)d->w * (size_t)d->h * 4);
  return true;
}

static void record_stop(Driver *d) {
  if (!d->rec_fd) return;
  close(d->rec_fd);
  d->rec_fd = 0;
  int st;
  waitpid(d->rec_pid, &st, 0);
  free(d->rec_px);
  d->rec_px = NULL;
}

static bool app_step(Driver *d, const char *a, char *arg) {
  float x, y;
  if (!strcmp(a, "press") || !strcmp(a, "dpress")) {
    if (locate(d, arg, &x, &y, NULL)) click_at(d, x, y, 0, !strcmp(a, "dpress"));
  } else if (!strcmp(a, "hover")) {
    if (locate(d, arg, &x, &y, NULL)) go_to(d, x, y);
  } else if (!strcmp(a, "dragby") || !strcmp(a, "rdrag")) {
    char *p = strchr(arg, '|');
    if (!p) return false;
    *p = 0;
    float dx = 0, dy = 0;
    sscanf(p + 1, "%f|%f", &dx, &dy);
    if (locate(d, arg, &x, &y, NULL)) drag_path(d, x, y, x + dx, y + dy, !strcmp(a, "dragby") ? 0 : 1);
  } else if (!strcmp(a, "field") || !strcmp(a, "pick")) {
    char *eq = strchr(arg, '=');
    if (!eq) return false;
    *eq = 0;
    const char *val = eq + 1;
    if (!strcmp(a, "field")) {
      if (!locate(d, arg, &x, &y, NULL)) return true;
      // A number field takes typing after a double-click; a text field after a single one.
      click_at(d, x, y, 0, true);
      d->in.ctrl = true;
      d->in.keys[KEY_A] = true;
      frame(d, DT);
      d->in.ctrl = false;
      type_text(d, val);
      d->in.keys[KEY_ENTER] = true;
      frame(d, DT);
      frame(d, DT);
      pause_frames(d, 12);
    } else {
      if (!locate(d, arg, &x, &y, NULL)) return true;
      click_at(d, x, y, 0, false);
      char opt[300];
      snprintf(opt, sizeof opt, "%s=%s", arg, val);
      if (locate(d, opt, &x, &y, NULL)) click_at(d, x, y, 0, false);
    }
  } else if (!strcmp(a, "expect")) {
    char *eq = strchr(arg, '=');
    if (!eq) return false;
    *eq = 0;
    char got[1024];
    if (!editor_query(d->e, arg, got, sizeof got)) {
      fprintf(stderr, "FAIL: nothing answers to \"%s\"\n", arg);
      d->failed = 1;
    } else if (strcmp(got, eq + 1)) {
      fprintf(stderr, "FAIL: %s is \"%s\", expected \"%s\"\n", arg, got, eq + 1);
      d->failed = 1;
    } else {
      printf("ok %s=%s\n", arg, got);
    }
  } else if (!strcmp(a, "marks")) {
    ui_dump_marks();
  } else if (!strcmp(a, "record")) {
    if (*arg) record_start(d, arg);
    else record_stop(d);
  } else if (!strcmp(a, "print")) {
    char got[1024];
    if (editor_query(d->e, arg, got, sizeof got)) printf("%s=%s\n", arg, got);
  } else {
    return false;
  }
  return true;
}

int cmd_script(int argc, char **argv) {
  bool app = argc > 0 && !strcmp(argv[0], "--app");
  if (app) argc--, argv++;
  if (argc < 1) return fprintf(stderr, "usage: 3disle script [--app] <file.json> [steps...]\n"), 2;
  char err[2048] = "";
  if (!headless_init(err, sizeof err) || !r_init(err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  ui_init();
  Driver d = {.w = 1400, .h = 850};
  d.in.scale = 1;
  d.fbo = r_offscreen_fbo(d.w, d.h);
  r_set_size(d.w, d.h, 1.5f, 4);
  if (app) {
    ui_record(true);
    d.e = editor_new("public");
    const char *file = strcmp(argv[0], "-") ? argv[0] : NULL;
    if (!editor_open(d.e, file, err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  } else {
    d.v = viewer_new("public", false);
    if (!viewer_open(d.v, argv[0], err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  }
  frame(&d, DT);
  for (int i = 1; i < argc && !d.failed; i++) {
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
      float x, y;
      sscanf(arg, "%f,%f", &x, &y);
      go_to(&d, x, y);
      frame(&d, DT);
    } else if (!strcmp(a, "click") || !strcmp(a, "rclick") || !strcmp(a, "dclick")) {
      float x, y;
      sscanf(arg, "%f,%f", &x, &y);
      click_at(&d, x, y, !strcmp(a, "rclick") ? 1 : 0, !strcmp(a, "dclick"));
    } else if (!strcmp(a, "drag")) {
      float x0, y0, x1, y1;
      sscanf(arg, "%f,%f,%f,%f", &x0, &y0, &x1, &y1);
      drag_path(&d, x0, y0, x1, y1, 0);
    } else if (!strcmp(a, "wheel")) {
      d.in.wheel = (float)atof(arg);
      frame(&d, DT);
    } else if (!strcmp(a, "key") || !strcmp(a, "ctrl")) {
      if (!strcmp(a, "ctrl")) d.in.ctrl = true;
      for (int k = 0; k < COUNT(KEYS); k++)
        if (!strcmp(KEYS[k].name, arg)) d.in.keys[KEYS[k].key] = true;
      frame(&d, DT);
      d.in.ctrl = false;
    } else if (!strcmp(a, "shift")) {
      d.in.shift = atoi(arg) != 0;
    } else if (!strcmp(a, "type")) {
      type_text(&d, arg);
    } else if (!strcmp(a, "shot")) {
      frame(&d, DT);
      shot(&d, arg);
    } else if (d.e && app_step(&d, a, arg)) {
      // done
    } else if (d.v && !strcmp(a, "explode")) {
      viewer_set_explode(d.v, (float)atof(arg));
      frame(&d, DT);
    } else if (d.v && !strcmp(a, "labels")) {
      viewer_set_labels(d.v, atoi(arg) != 0);
      frame(&d, DT);
    } else if (d.v && !strcmp(a, "select")) {
      viewer_select(d.v, model_find(viewer_model(d.v), arg), -1);
      frame(&d, DT);
    } else if (d.v && !strcmp(a, "step")) {
      viewer_step(d.v, atoi(arg));
      frame(&d, DT);
    } else if (d.v && !strcmp(a, "print")) {
      printf("%s: title=%s level=%d\n", arg, viewer_title(d.v), viewer_level(d.v));
    } else {
      fprintf(stderr, "unknown step %s\n", a);
      return 2;
    }
  }
  record_stop(&d);
  GLenum e = glGetError();
  if (e) printf("GL error 0x%x\n", e);
  if (d.e) editor_free(d.e);
  if (d.v) viewer_free(d.v);
  headless_shutdown();
  return d.failed;
}
