// The exported page: the viewer on a canvas. wasm/glue.js feeds it input and asks for frames; the data
// sits in the page and reaches file_read through the import below.
#include "gl.h"
#include "render.h"
#include "ui.h"
#include "viewer.h"

#define EXPORT __attribute__((visibility("default")))

int js_file_size(const char *path);
void js_file_read(const char *path, char *out);

char *file_read(const char *path, size_t *len) {
  int n = js_file_size(path);
  if (n < 0) return NULL;
  char *d = malloc((size_t)n + 1);
  js_file_read(path, d);
  d[n] = 0;
  if (len) *len = (size_t)n;
  return d;
}
bool file_write(const char *path, const void *data, size_t len) { return false; }

static Viewer *V;
static UiInput IN;
static char BUF[4096];
static char TEXT[256];
static float RENDER_SCALE = 1;

EXPORT char *web_buf(void) { return BUF; }

// BUF holds the entry file's path.
EXPORT int web_start(void) {
  char err[1024] = "";
  if (!r_init(err, sizeof err)) {
    printf("%s", err);
    return 0;
  }
  ui_init();
  V = viewer_new("", true);
  if (!viewer_open(V, BUF, err, sizeof err)) {
    printf("%s", err);
    return 0;
  }
  return 1;
}

EXPORT void web_resize(int w, int h, float scale) {
  IN.w = w, IN.h = h, IN.scale = scale;
  // Above 1x on low-density screens: fewer shimmering edges. As the desktop app does.
  RENDER_SCALE = scale >= 1.5f ? 1 : 1.25f;
  r_set_size(w, h, RENDER_SCALE, 4);
}

EXPORT void web_mouse(float x, float y) { IN.mx = x, IN.my = y; }
EXPORT void web_button(int b, int down, int twice) {
  if (b < 0 || b > 2) return;
  IN.down[b] = down;
  if (down) {
    IN.pressed[b] = true;
    if (twice) IN.double_click = true;
  } else {
    IN.released[b] = true;
  }
}
EXPORT void web_wheel(float v) { IN.wheel += v; }
EXPORT void web_mods(int ctrl, int shift) { IN.ctrl = ctrl, IN.shift = shift; }

// BUF holds the browser's name for the key.
EXPORT void web_key(void) {
  static const struct {
    const char *name;
    UiKey key;
  } MAP[] = {{"ArrowLeft", KEY_LEFT}, {"ArrowRight", KEY_RIGHT}, {"ArrowUp", KEY_UP}, {"ArrowDown", KEY_DOWN}, {"Enter", KEY_ENTER},
             {"Escape", KEY_ESCAPE}, {"Backspace", KEY_BACKSPACE}, {"Delete", KEY_DELETE}, {"Home", KEY_HOME}, {"End", KEY_END},
             {"Tab", KEY_TAB}, {" ", KEY_SPACE}, {"l", KEY_L}, {"L", KEY_L}};
  for (int i = 0; i < COUNT(MAP); i++)
    if (!strcmp(MAP[i].name, BUF)) IN.keys[MAP[i].key] = true;
}

EXPORT void web_text(void) {
  size_t n = strlen(TEXT);
  snprintf(TEXT + n, sizeof TEXT - n, "%s", BUF);
  IN.text = TEXT;
}

EXPORT void web_frame(float dt) {
  IN.dt = dt;
  viewer_frame(V, &IN, 0);
  memset(IN.pressed, 0, sizeof IN.pressed);
  memset(IN.released, 0, sizeof IN.released);
  memset(IN.keys, 0, sizeof IN.keys);
  IN.wheel = 0;
  IN.text = NULL;
  IN.double_click = false;
  TEXT[0] = 0;
}

EXPORT const char *web_title(void) { return viewer_title(V); }
