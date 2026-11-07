// The desktop window: SDL3 for the window and input, nothing else.
#include "app.h"
#include "editor.h"
#include "gl.h"
#include "render.h"
#include "ui.h"
#include "viewer.h"
#include <SDL3/SDL.h>
#include <unistd.h>

// The data sits next to the program, wherever it is started from.
static const char *find_root(char *buf, size_t n) {
  if (!access("public/models/index.json", R_OK)) return "public";
  char exe[512];
  ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
  if (len > 0) {
    exe[len] = 0;
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    snprintf(buf, n, "%s/../public", exe);
    char probe[640];
    snprintf(probe, sizeof probe, "%s/models/index.json", buf);
    if (!access(probe, R_OK)) return buf;
  }
  return "public";
}

static UiKey map_key(SDL_Keycode k) {
  switch (k) {
  case SDLK_LEFT: return KEY_LEFT;
  case SDLK_RIGHT: return KEY_RIGHT;
  case SDLK_UP: return KEY_UP;
  case SDLK_DOWN: return KEY_DOWN;
  case SDLK_RETURN:
  case SDLK_KP_ENTER: return KEY_ENTER;
  case SDLK_ESCAPE: return KEY_ESCAPE;
  case SDLK_BACKSPACE: return KEY_BACKSPACE;
  case SDLK_DELETE: return KEY_DELETE;
  case SDLK_HOME: return KEY_HOME;
  case SDLK_END: return KEY_END;
  case SDLK_TAB: return KEY_TAB;
  case SDLK_SPACE: return KEY_SPACE;
  case SDLK_A: return KEY_A;
  case SDLK_C: return KEY_C;
  case SDLK_D: return KEY_D;
  case SDLK_E: return KEY_E;
  case SDLK_F: return KEY_F;
  case SDLK_G: return KEY_G;
  case SDLK_K: return KEY_K;
  case SDLK_L: return KEY_L;
  case SDLK_N: return KEY_N;
  case SDLK_O: return KEY_O;
  case SDLK_R: return KEY_R;
  case SDLK_S: return KEY_S;
  case SDLK_V: return KEY_V;
  case SDLK_X: return KEY_X;
  case SDLK_Y: return KEY_Y;
  case SDLK_Z: return KEY_Z;
  case SDLK_1: return KEY_1;
  case SDLK_2: return KEY_2;
  case SDLK_3: return KEY_3;
  default: return KEY_NONE;
  }
}

int app_window(int argc, char **argv) {
  if (!SDL_Init(SDL_INIT_VIDEO)) return fprintf(stderr, "SDL: %s\n", SDL_GetError()), 1;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  SDL_Window *win = SDL_CreateWindow("3Disle", 1400, 850, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!win) return fprintf(stderr, "SDL: %s\n", SDL_GetError()), 1;
  SDL_GLContext ctx = SDL_GL_CreateContext(win);
  if (!ctx) return fprintf(stderr, "SDL: %s\n", SDL_GetError()), 1;
  SDL_GL_SetSwapInterval(1);
  char err[2048] = "";
  if (!r_init(err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  ui_init();
  char rootbuf[560], fallback[640];
  const char *root = find_root(rootbuf, sizeof rootbuf);
  // `view` is the visitor's viewer alone; anything else is the app.
  bool view_only = argc > 0 && !strcmp(argv[0], "view");
  Viewer *v = NULL;
  Editor *ed = NULL;
  if (view_only) {
    v = viewer_new(root, false);
    snprintf(fallback, sizeof fallback, "%s/models/drone/tour.json", root);
    const char *file = argc > 1 ? argv[1] : fallback;
    if (!viewer_open(v, file, err, sizeof err)) {
      fprintf(stderr, "%s\n", err);
      viewer_set_status(v, err);
    }
  } else {
    ed = editor_new(root);
    if (!editor_open(ed, argc > 0 ? argv[0] : NULL, err, sizeof err)) {
      fprintf(stderr, "%s\n", err);
      editor_open(ed, NULL, err, sizeof err);
    }
  }
  SDL_StartTextInput(win);

  UiInput in = {0};
  char text[256];
  Uint64 last = SDL_GetPerformanceCounter();
  int rw = 0, rh = 0;
  bool running = true;
  while (running) {
    memset(in.pressed, 0, sizeof in.pressed);
    memset(in.released, 0, sizeof in.released);
    memset(in.keys, 0, sizeof in.keys);
    in.wheel = 0;
    in.text = NULL;
    in.double_click = false;
    text[0] = 0;
    float density = SDL_GetWindowPixelDensity(win);
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
      case SDL_EVENT_QUIT:
        if (!ed || editor_request_close(ed)) running = false;
        break;
      case SDL_EVENT_MOUSE_MOTION:
        in.mx = e.motion.x * density;
        in.my = e.motion.y * density;
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        int b = e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_RIGHT ? 1 : e.button.button == SDL_BUTTON_MIDDLE ? 2 : -1;
        if (b < 0) break;
        in.mx = e.button.x * density;
        in.my = e.button.y * density;
        in.down[b] = e.button.down;
        if (e.button.down) {
          in.pressed[b] = true;
          if (b == 0 && e.button.clicks == 2) in.double_click = true;
        } else {
          in.released[b] = true;
        }
        break;
      }
      case SDL_EVENT_MOUSE_WHEEL: in.wheel += e.wheel.y; break;
      case SDL_EVENT_KEY_DOWN: {
        UiKey k = map_key(e.key.key);
        if (k) in.keys[k] = true;
        break;
      }
      case SDL_EVENT_TEXT_INPUT:
        strncat(text, e.text.text, sizeof text - strlen(text) - 1);
        in.text = text;
        break;
      default: break;
      }
    }
    SDL_Keymod mod = SDL_GetModState();
    in.ctrl = mod & SDL_KMOD_CTRL;
    in.shift = mod & SDL_KMOD_SHIFT;
    int w, h;
    SDL_GetWindowSizeInPixels(win, &w, &h);
    in.w = w, in.h = h;
    in.scale = density;
    if (w != rw || h != rh) {
      // Above 1x on low-density screens: fewer shimmering edges.
      r_set_size(w, h, density >= 1.5f ? 1.0f : 1.5f, 4);
      rw = w, rh = h;
    }
    Uint64 now = SDL_GetPerformanceCounter();
    in.dt = (float)((double)(now - last) / (double)SDL_GetPerformanceFrequency());
    last = now;
    if (ed) {
      editor_frame(ed, &in, 0);
      if (editor_should_close(ed)) running = false;
    } else {
      viewer_frame(v, &in, 0);
    }
    SDL_GL_SwapWindow(win);
  }
  if (v) viewer_free(v);
  if (ed) editor_free(ed);
  SDL_GL_DestroyContext(ctx);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
