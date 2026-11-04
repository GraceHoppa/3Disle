#include "ui.h"
#include "gl.h"
#include "paint.h"
#include "shaders_data.h"
#include "stb_truetype.h"

#define ATLAS 2048
#define GLSL_HEAD "#version 300 es\nprecision highp float;\n"

typedef struct {
  float x, y, u, v;
  u32 c;
} UiVert;

typedef struct {
  int start, count;
  Rect clip;
  bool clipped;
} Cmd;

typedef struct {
  u32 cp;
  int font;
  int px;
  float u0, v0, u1, v1;
  int w, h, xoff, yoff;
  float adv;
} Glyph;

typedef struct {
  UiVert *v;
  Cmd *cmds;
} Layer;

static struct {
  UiInput in;
  Layer layers[2];
  int layer;
  u32 prog, vao, vbo, tex;
  int loc_size;
  stbtt_fontinfo fonts[2];
  Glyph *glyphs;
  int *table; // open-addressed index into glyphs
  int table_size;
  int ax, ay, arow;
  u8 *atlas_clear;
  Rect clips[16];
  int nclips;
  u32 hot, active, focus, last_focus_widget;
  bool focus_next;
  u32 id_stack[16];
  int nids;
  Rect *blocks, *blocks_prev;
  Rect popup, popup_next;
  bool popup_on, popup_next_on;
  u32 open_popup;
  char edit[4096];
  int cursor;
  bool select_all;
  u32 edit_id;
  bool editing_drag;
  bool any_active;
  double drag_start;
  float drag_mx;
  struct {
    u32 id;
    float v;
  } states[1024];
  float blink;
} U;

static u32 hash_str(u32 h, const char *s) {
  while (*s) h = (h ^ (u8)*s++) * 16777619u;
  return h;
}
static u32 hash_int(u32 h, int v) { return (h ^ (u32)v) * 16777619u; }

const UiInput *ui_input(void) { return &U.in; }

static const char *VS = GLSL_HEAD "layout(location=0) in vec2 aPos;layout(location=1) in vec2 aUv;layout(location=2) in vec4 aCol;"
                                  "uniform vec2 uSize;out vec2 vUv;out vec4 vCol;void main(){vUv=aUv;vCol=aCol;"
                                  "gl_Position=vec4(aPos.x/uSize.x*2.0-1.0,1.0-aPos.y/uSize.y*2.0,0.0,1.0);}";
static const char *FS = GLSL_HEAD "uniform sampler2D uTex;in vec2 vUv;in vec4 vCol;out vec4 o;"
                                  "void main(){o=vec4(vCol.rgb,vCol.a*texture(uTex,vUv).r);}";

static u32 sh(GLenum t, const char *src) {
  u32 s = glCreateShader(t);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  return s;
}

static void atlas_reset(void) {
  arr_clear(U.glyphs);
  for (int i = 0; i < U.table_size; i++) U.table[i] = -1;
  U.ax = 4, U.ay = 0, U.arow = 4;
  u8 white[16];
  memset(white, 255, sizeof white);
  glBindTexture(GL_TEXTURE_2D, U.tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RED, GL_UNSIGNED_BYTE, white);
}

void ui_init(void) {
  U.prog = glCreateProgram();
  u32 v = sh(GL_VERTEX_SHADER, VS), f = sh(GL_FRAGMENT_SHADER, FS);
  glAttachShader(U.prog, v);
  glAttachShader(U.prog, f);
  glLinkProgram(U.prog);
  U.loc_size = glGetUniformLocation(U.prog, "uSize");
  glGenVertexArrays(1, &U.vao);
  glGenBuffers(1, &U.vbo);
  glBindVertexArray(U.vao);
  glBindBuffer(GL_ARRAY_BUFFER, U.vbo);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVert), (void *)0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVert), (void *)8);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UiVert), (void *)16);
  glBindVertexArray(0);
  glGenTextures(1, &U.tex);
  glBindTexture(GL_TEXTURE_2D, U.tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8, ATLAS, ATLAS);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  u8 *zero = calloc(ATLAS, ATLAS);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ATLAS, ATLAS, GL_RED, GL_UNSIGNED_BYTE, zero);
  free(zero);
  stbtt_InitFont(&U.fonts[0], font_regular(), 0);
  stbtt_InitFont(&U.fonts[1], font_bold(), 0);
  U.table_size = 8192;
  U.table = malloc(sizeof(int) * (size_t)U.table_size);
  atlas_reset();
}

void ui_begin(const UiInput *in) {
  U.in = *in;
  if (U.in.scale <= 0) U.in.scale = 1;
  for (int i = 0; i < 2; i++) {
    arr_clear(U.layers[i].v);
    arr_clear(U.layers[i].cmds);
  }
  U.layer = 0;
  U.nclips = 0;
  U.nids = 0;
  // What was interface last frame answers this frame's mouse.
  Rect *t = U.blocks_prev;
  U.blocks_prev = U.blocks;
  U.blocks = t;
  arr_clear(U.blocks);
  U.popup = U.popup_next;
  U.popup_on = U.popup_next_on;
  U.popup_next_on = false;
  U.hot = 0;
  U.blink += in->dt;
  if (U.active && !in->down[0]) U.active = 0;
}

static Layer *L(void) { return &U.layers[U.layer]; }

static void ensure_cmd(void) {
  Layer *l = L();
  bool clipped = U.nclips > 0;
  Rect clip = clipped ? U.clips[U.nclips - 1] : rect(0, 0, 0, 0);
  if (arr_len(l->cmds)) {
    Cmd *c = &arr_last(l->cmds);
    if (c->clipped == clipped && (!clipped || !memcmp(&c->clip, &clip, sizeof clip))) return;
  }
  arr_push(l->cmds, ((Cmd){arr_len(l->v), 0, clip, clipped}));
}

static void add_tri_verts(UiVert a, UiVert b, UiVert c) {
  ensure_cmd();
  Layer *l = L();
  UiVert *d = arr_add(l->v, 3);
  d[0] = a, d[1] = b, d[2] = c;
  arr_last(l->cmds).count += 3;
}
static void add_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, u32 c0, u32 c1) {
  UiVert a = {x0, y0, u0, v0, c0}, b = {x1, y0, u1, v0, c0}, c = {x1, y1, u1, v1, c1}, d = {x0, y1, u0, v1, c1};
  add_tri_verts(a, b, c);
  add_tri_verts(a, c, d);
}

#define WHITE_UV (1.5f / ATLAS)
void ui_rect(Rect r, u32 c) { add_quad(r.x, r.y, r.x + r.w, r.y + r.h, WHITE_UV, WHITE_UV, WHITE_UV, WHITE_UV, c, c); }
void ui_rect_grad(Rect r, u32 top, u32 bottom) {
  add_quad(r.x, r.y, r.x + r.w, r.y + r.h, WHITE_UV, WHITE_UV, WHITE_UV, WHITE_UV, top, bottom);
}
void ui_border(Rect r, u32 c, float t) {
  ui_rect(rect(r.x, r.y, r.w, t), c);
  ui_rect(rect(r.x, r.y + r.h - t, r.w, t), c);
  ui_rect(rect(r.x, r.y + t, t, r.h - 2 * t), c);
  ui_rect(rect(r.x + r.w - t, r.y + t, t, r.h - 2 * t), c);
}
void ui_panel(Rect r) {
  ui_rect_grad(r, C_PANEL_TOP, C_PANEL_BOT);
  ui_border(r, C_LINE, maxf(1, S(1)));
  ui_block(r);
}
void ui_line(float x0, float y0, float x1, float y1, float w, u32 c) {
  float dx = x1 - x0, dy = y1 - y0, l = hypotf(dx, dy);
  if (l < 1e-4f) return;
  dx /= l, dy /= l;
  float nx = -dy, ny = dx, hw = w / 2, f = hw + 0.75f;
  u32 z = c & 0xffffff;
  const float u = WHITE_UV;
  // Solid core with a soft edge on both sides, so a line at any angle stays smooth.
  float core = maxf(hw - 0.25f, 0);
  UiVert a = {x0 + nx * core, y0 + ny * core, u, u, c}, b = {x1 + nx * core, y1 + ny * core, u, u, c};
  UiVert d = {x0 - nx * core, y0 - ny * core, u, u, c}, e = {x1 - nx * core, y1 - ny * core, u, u, c};
  UiVert oa = {x0 + nx * f, y0 + ny * f, u, u, z}, ob = {x1 + nx * f, y1 + ny * f, u, u, z};
  UiVert od = {x0 - nx * f, y0 - ny * f, u, u, z}, oe = {x1 - nx * f, y1 - ny * f, u, u, z};
  add_tri_verts(a, b, e);
  add_tri_verts(a, e, d);
  add_tri_verts(oa, ob, b);
  add_tri_verts(oa, b, a);
  add_tri_verts(d, e, oe);
  add_tri_verts(d, oe, od);
}
void ui_circle(float cx, float cy, float r, u32 c) {
  const int N = 28;
  const float u = WHITE_UV;
  u32 z = c & 0xffffff;
  for (int i = 0; i < N; i++) {
    float a0 = (float)i / (float)N * TAU, a1 = (float)(i + 1) / (float)N * TAU;
    float ca = cosf(a0), sa = sinf(a0), cb = cosf(a1), sb = sinf(a1);
    UiVert m = {cx, cy, u, u, c}, p = {cx + ca * r, cy + sa * r, u, u, c}, q = {cx + cb * r, cy + sb * r, u, u, c};
    UiVert op = {cx + ca * (r + 1), cy + sa * (r + 1), u, u, z}, oq = {cx + cb * (r + 1), cy + sb * (r + 1), u, u, z};
    add_tri_verts(m, p, q);
    add_tri_verts(p, op, oq);
    add_tri_verts(p, oq, q);
  }
}

// ------------------------------------------------------------------ text
static u32 decode(const char **s) {
  const unsigned char *c = (const unsigned char *)*s;
  u32 cp;
  if (*c < 0x80) cp = *c, *s += 1;
  else if (*c < 0xE0 && c[1]) cp = (u32)(c[0] & 31) << 6 | (c[1] & 63), *s += 2;
  else if (*c < 0xF0 && c[1] && c[2]) cp = (u32)(c[0] & 15) << 12 | (u32)(c[1] & 63) << 6 | (c[2] & 63), *s += 3;
  else if (c[1] && c[2] && c[3]) cp = (u32)(c[0] & 7) << 18 | (u32)(c[1] & 63) << 12 | (u32)(c[2] & 63) << 6 | (c[3] & 63), *s += 4;
  else cp = '?', *s += 1;
  return cp;
}

static const Glyph *glyph(int font, int px, u32 cp) {
  u32 h = hash_int(hash_int(hash_int(2166136261u, font), px), (int)cp);
  int slot = (int)(h % (u32)U.table_size);
  for (int probe = 0; probe < U.table_size; probe++, slot = (slot + 1) % U.table_size) {
    int i = U.table[slot];
    if (i < 0) break;
    Glyph *g = &U.glyphs[i];
    if (g->cp == cp && g->font == font && g->px == px) return g;
  }
  stbtt_fontinfo *fi = &U.fonts[font];
  float sc = stbtt_ScaleForPixelHeight(fi, (float)px);
  int x0, y0, x1, y1, adv, lsb;
  stbtt_GetCodepointBitmapBox(fi, (int)cp, sc, sc, &x0, &y0, &x1, &y1);
  stbtt_GetCodepointHMetrics(fi, (int)cp, &adv, &lsb);
  int w = x1 - x0, h2 = y1 - y0;
  if (U.ax + w + 2 >= ATLAS) U.ax = 4, U.ay += U.arow + 2, U.arow = 0;
  if (U.ay + h2 + 2 >= ATLAS || arr_len(U.glyphs) >= U.table_size / 2) {
    atlas_reset(); // full: start over, the rest of this frame fills it again
    return glyph(font, px, cp);
  }
  Glyph g = {cp, font, px, 0, 0, 0, 0, w, h2, x0, y0, (float)adv * sc};
  if (w > 0 && h2 > 0) {
    u8 *bmp = malloc((size_t)w * (size_t)h2);
    stbtt_MakeCodepointBitmap(fi, bmp, w, h2, w, sc, sc, (int)cp);
    glBindTexture(GL_TEXTURE_2D, U.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, U.ax, U.ay, w, h2, GL_RED, GL_UNSIGNED_BYTE, bmp);
    free(bmp);
    g.u0 = (float)U.ax / ATLAS, g.v0 = (float)U.ay / ATLAS;
    g.u1 = (float)(U.ax + w) / ATLAS, g.v1 = (float)(U.ay + h2) / ATLAS;
    U.ax += w + 2;
    U.arow = maxi(U.arow, h2);
  }
  arr_push(U.glyphs, g);
  U.table[slot] = arr_len(U.glyphs) - 1;
  return &arr_last(U.glyphs);
}

static float ascent_of(float size, Font f) {
  int a, d, l;
  stbtt_GetFontVMetrics(&U.fonts[f], &a, &d, &l);
  return (float)a * stbtt_ScaleForPixelHeight(&U.fonts[f], size);
}

float ui_line_height(float size) { return roundf(size * 1.32f); }

float ui_text_sp(float x, float y, float size, Font f, u32 c, float spacing, const char *s) {
  int px = maxi((int)lroundf(size), 1);
  float pen = roundf(x), base = roundf(y + ascent_of((float)px, f) + (ui_line_height(size) - (float)px) * 0.15f);
  for (const char *t = s; *t;) {
    u32 cp = decode(&t);
    const Glyph *g = glyph(f, px, cp);
    if (g->w > 0) {
      float gx = roundf(pen) + (float)g->xoff, gy = base + (float)g->yoff;
      add_quad(gx, gy, gx + (float)g->w, gy + (float)g->h, g->u0, g->v0, g->u1, g->v1, c, c);
    }
    pen += g->adv + spacing;
  }
  return pen - x;
}
float ui_text(float x, float y, float size, Font f, u32 c, const char *s) { return ui_text_sp(x, y, size, f, c, 0, s); }

float ui_text_width(float size, Font f, float spacing, const char *s) {
  int px = maxi((int)lroundf(size), 1);
  float w = 0;
  for (const char *t = s; *t;) w += glyph(f, px, decode(&t))->adv + spacing;
  return w;
}

// Greedy wrap. With draw off it only measures.
static float wrap_run(float x, float y, float w, float size, Font f, u32 c, float lh, const char *s, bool draw) {
  float cy = y;
  char line[1024];
  const char *p = s;
  while (*p) {
    int n = 0, last_space = -1;
    const char *q = p;
    float lw = 0;
    int px = maxi((int)lroundf(size), 1);
    while (*q && *q != '\n' && n < (int)sizeof line - 5) {
      const char *start = q;
      u32 cp = decode(&q);
      float adv = glyph(f, px, cp)->adv;
      if (lw + adv > w && n > 0) {
        q = start;
        break;
      }
      lw += adv;
      if (cp == ' ') last_space = n;
      memcpy(line + n, start, (size_t)(q - start));
      n += (int)(q - start);
    }
    bool broke = *q && *q != '\n';
    if (broke && last_space > 0) {
      q = p + last_space + 1;
      n = last_space;
    }
    line[n] = 0;
    if (draw) ui_text(x, cy, size, f, c, line);
    cy += lh;
    p = q;
    if (*p == '\n') p++;
  }
  return cy - y;
}
float ui_wrap(float x, float y, float w, float size, Font f, u32 c, float lh, const char *s) {
  return wrap_run(x, y, w, size, f, c, lh, s, true);
}
float ui_wrap_height(float w, float size, Font f, float lh, const char *s) { return wrap_run(0, 0, w, size, f, 0, lh, s, false); }

void ui_text_fit(float x, float y, float w, float size, Font f, u32 c, const char *s) {
  if (ui_text_width(size, f, 0, s) <= w) {
    ui_text(x, y, size, f, c, s);
    return;
  }
  char buf[512];
  float ell = ui_text_width(size, f, 0, "\xE2\x80\xA6"), run = 0;
  int n = 0, px = maxi((int)lroundf(size), 1);
  for (const char *t = s; *t && n < (int)sizeof buf - 8;) {
    const char *st = t;
    float a = glyph(f, px, decode(&t))->adv;
    if (run + a + ell > w) break;
    run += a;
    memcpy(buf + n, st, (size_t)(t - st));
    n += (int)(t - st);
  }
  memcpy(buf + n, "\xE2\x80\xA6", 4);
  ui_text(x, y, size, f, c, buf);
}

void ui_clip_push(Rect r) {
  if (U.nclips) {
    Rect p = U.clips[U.nclips - 1];
    float x0 = maxf(r.x, p.x), y0 = maxf(r.y, p.y), x1 = minf(r.x + r.w, p.x + p.w), y1 = minf(r.y + r.h, p.y + p.h);
    r = rect(x0, y0, maxf(x1 - x0, 0), maxf(y1 - y0, 0));
  }
  if (U.nclips < COUNT(U.clips)) U.clips[U.nclips++] = r;
}
void ui_clip_pop(void) {
  if (U.nclips) U.nclips--;
}
void ui_set_layer(int layer) { U.layer = layer; }

void ui_render(u32 fbo) {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, U.in.w, U.in.h);
  glUseProgram(U.prog);
  glUniform2f(U.loc_size, (float)U.in.w, (float)U.in.h);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, U.tex);
  glBindVertexArray(U.vao);
  glBindBuffer(GL_ARRAY_BUFFER, U.vbo);
  for (int li = 0; li < 2; li++) {
    Layer *l = &U.layers[li];
    if (!arr_len(l->v)) continue;
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(UiVert) * (size_t)arr_len(l->v)), l->v, GL_STREAM_DRAW);
    for (int i = 0; i < arr_len(l->cmds); i++) {
      Cmd *c = &l->cmds[i];
      if (!c->count) continue;
      if (c->clipped) {
        glEnable(GL_SCISSOR_TEST);
        glScissor((int)c->clip.x, U.in.h - (int)(c->clip.y + c->clip.h), (int)c->clip.w, (int)c->clip.h);
      } else {
        glDisable(GL_SCISSOR_TEST);
      }
      glDrawArrays(GL_TRIANGLES, c->start, c->count);
    }
  }
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glBindVertexArray(0);
}

// ------------------------------------------------------------------ ids and state
u32 ui_id(const char *s) {
  u32 h = U.nids ? U.id_stack[U.nids - 1] : 2166136261u;
  return hash_str(h, s) | 1;
}
void ui_push_id(int v) {
  u32 h = U.nids ? U.id_stack[U.nids - 1] : 2166136261u;
  if (U.nids < COUNT(U.id_stack)) U.id_stack[U.nids++] = hash_int(h, v);
}
void ui_pop_id(void) {
  if (U.nids) U.nids--;
}
float *ui_state(u32 id) {
  int n = COUNT(U.states);
  int slot = (int)(id % (u32)n);
  for (int i = 0; i < n; i++, slot = (slot + 1) % n) {
    if (U.states[slot].id == id) return &U.states[slot].v;
    if (!U.states[slot].id) {
      U.states[slot].id = id;
      U.states[slot].v = 0;
      return &U.states[slot].v;
    }
  }
  return &U.states[0].v;
}

void ui_block(Rect r) { arr_push(U.blocks, r); }
bool ui_wants_mouse(void) {
  if (U.active) return true;
  if (U.popup_on && rect_has(U.popup, U.in.mx, U.in.my)) return true;
  for (int i = 0; i < arr_len(U.blocks_prev); i++)
    if (rect_has(U.blocks_prev[i], U.in.mx, U.in.my)) return true;
  return false;
}
bool ui_wants_keyboard(void) { return U.focus != 0; }
void ui_set_popup(Rect r) {
  U.popup_next = r;
  U.popup_next_on = true;
}
bool ui_hovering(Rect r) {
  if (!rect_has(r, U.in.mx, U.in.my)) return false;
  if (U.nclips && !rect_has(U.clips[U.nclips - 1], U.in.mx, U.in.my)) return false;
  if (U.layer == 0 && U.popup_on && rect_has(U.popup, U.in.mx, U.in.my)) return false;
  return true;
}
void ui_unfocus(void) { U.focus = 0; }
void ui_focus_next(void) { U.focus_next = true; }

// ------------------------------------------------------------------ widgets
bool ui_button(Rect r, const char *label, int flags) {
  u32 id = ui_id(label);
  bool dis = flags & BTN_DISABLED;
  bool over = !dis && ui_hovering(r);
  bool clicked = false;
  if (over && U.in.pressed[0] && !U.active) U.active = id, U.focus = 0;
  if (over && U.active == id && U.in.released[0]) clicked = true;
  if (flags & BTN_ON) ui_rect(r, RGBA(28, 50, 14, 255));
  else if (over) ui_rect(r, C_HOVER);
  else if (!(flags & BTN_QUIET)) ui_rect(r, RGBA(6, 12, 8, 230));
  if (!(flags & BTN_QUIET)) ui_border(r, flags & BTN_ON ? C_ACCENT : (over ? C_ACCENT : C_LINE), maxf(1, S(1)));
  float size = S(11);
  char text[128];
  snprintf(text, sizeof text, "%s", label);
  char *hash = strstr(text, "##");
  if (hash) *hash = 0;
  float tw = ui_text_width(size, FONT_REGULAR, S(1), text);
  float tx = flags & BTN_LEFT ? r.x + S(8) : r.x + (r.w - tw) / 2;
  u32 col = dis ? C_FAINT : (flags & BTN_ON ? C_BRIGHT : C_TEXT);
  ui_text_sp(tx, r.y + (r.h - ui_line_height(size)) / 2, size, FONT_REGULAR, col, S(1), text);
  return clicked;
}

bool ui_slider(Rect r, const char *ids, float *v, float mn, float mx) {
  u32 id = ui_id(ids);
  float pad = S(8);
  bool over = ui_hovering(r);
  if (over && U.in.pressed[0] && !U.active) U.active = id, U.focus = 0;
  bool changed = false;
  if (U.active == id) {
    float t = clampf((U.in.mx - (r.x + pad)) / maxf(r.w - 2 * pad, 1), 0, 1);
    float nv = mn + (mx - mn) * t;
    changed = nv != *v;
    *v = nv;
  }
  float t = mx > mn ? clampf((*v - mn) / (mx - mn), 0, 1) : 0;
  float cy = r.y + r.h / 2, tr = maxf(S(3), 2);
  float x0 = r.x + pad, x1 = r.x + r.w - pad, kx = x0 + (x1 - x0) * t;
  ui_rect(rect(x0, cy - tr / 2, x1 - x0, tr), RGBA(60, 66, 62, 255));
  ui_rect(rect(x0, cy - tr / 2, kx - x0, tr), C_ACCENT);
  ui_circle(kx, cy, over || U.active == id ? S(7.5f) : S(6.5f), C_ACCENT);
  return changed;
}

static int utf8_prev(const char *s, int i) {
  i--;
  while (i > 0 && (s[i] & 0xC0) == 0x80) i--;
  return maxi(i, 0);
}
static int utf8_next(const char *s, int i) {
  if (!s[i]) return i;
  i++;
  while (s[i] && (s[i] & 0xC0) == 0x80) i++;
  return i;
}

// Shared by the single-line and multi-line fields: applies this frame's keys to the buffer.
static bool edit_apply(char *buf, int cap, bool multiline) {
  bool changed = false;
  int len = (int)strlen(buf);
  if (U.cursor > len) U.cursor = len;
  const UiInput *in = &U.in;
  if (in->ctrl && in->keys[KEY_A]) U.select_all = true;
  if ((in->text && *in->text) || in->keys[KEY_BACKSPACE] || in->keys[KEY_DELETE]) {
    if (U.select_all) {
      if (in->text && *in->text) buf[0] = 0;
      else buf[0] = 0;
      len = 0;
      U.cursor = 0;
      changed = true;
      if (!(in->text && *in->text)) {
        U.select_all = false;
        return true;
      }
    }
  }
  if (in->text) {
    int n = (int)strlen(in->text);
    if (len + n < cap) {
      memmove(buf + U.cursor + n, buf + U.cursor, (size_t)(len - U.cursor + 1));
      memcpy(buf + U.cursor, in->text, (size_t)n);
      U.cursor += n;
      len += n;
      changed = changed || n > 0;
    }
  }
  if (in->keys[KEY_BACKSPACE] && U.cursor > 0) {
    int p = utf8_prev(buf, U.cursor);
    memmove(buf + p, buf + U.cursor, (size_t)(len - U.cursor + 1));
    U.cursor = p;
    changed = true;
  }
  if (in->keys[KEY_DELETE] && U.cursor < len) {
    int n = utf8_next(buf, U.cursor);
    memmove(buf + U.cursor, buf + n, (size_t)(len - n + 1));
    changed = true;
  }
  if (in->keys[KEY_LEFT]) U.cursor = utf8_prev(buf, U.cursor);
  if (in->keys[KEY_RIGHT]) U.cursor = utf8_next(buf, U.cursor);
  if (in->keys[KEY_HOME]) U.cursor = 0;
  if (in->keys[KEY_END]) U.cursor = (int)strlen(buf);
  if (multiline && in->keys[KEY_ENTER] && len + 1 < cap) {
    memmove(buf + U.cursor + 1, buf + U.cursor, (size_t)(len - U.cursor + 1));
    buf[U.cursor++] = '\n';
    changed = true;
  }
  if ((in->text && *in->text) || in->keys[KEY_LEFT] || in->keys[KEY_RIGHT] || in->keys[KEY_HOME] || in->keys[KEY_END] || in->keys[KEY_BACKSPACE]) {
    U.select_all = false;
    U.blink = 0;
  }
  return changed;
}

bool ui_text_field(Rect r, const char *ids, char *buf, int cap, const char *hint) {
  u32 id = ui_id(ids);
  bool over = ui_hovering(r);
  if (over && U.in.pressed[0]) {
    if (U.focus != id) U.select_all = true;
    U.focus = id;
    U.cursor = (int)strlen(buf);
    U.blink = 0;
  } else if (U.focus == id && U.in.pressed[0] && !over) {
    U.focus = 0;
  }
  if (U.focus_next && U.last_focus_widget == U.focus && U.focus) {
    U.focus = id;
    U.focus_next = false;
    U.cursor = (int)strlen(buf);
    U.select_all = true;
  }
  ui_rect(r, C_FIELD);
  ui_border(r, U.focus == id ? C_ACCENT : (over ? RGBA(60, 90, 40, 255) : C_LINE), maxf(1, S(1)));
  float size = S(12), pad = S(6), ty = r.y + (r.h - ui_line_height(size)) / 2;
  bool changed = false;
  ui_clip_push(rect_inset(r, 1));
  if (U.focus == id) {
    changed = edit_apply(buf, cap, false);
    if (U.in.keys[KEY_ENTER] || U.in.keys[KEY_ESCAPE]) U.focus = 0;
    if (U.in.keys[KEY_TAB]) U.focus_next = true;
    U.last_focus_widget = id;
    char pre[1024];
    int n = mini(U.cursor, (int)sizeof pre - 1);
    memcpy(pre, buf, (size_t)n);
    pre[n] = 0;
    float cx = ui_text_width(size, FONT_REGULAR, 0, pre);
    float scroll = maxf(0, cx - (r.w - 2 * pad - S(4)));
    float full = ui_text_width(size, FONT_REGULAR, 0, buf);
    if (U.select_all && *buf) ui_rect(rect(r.x + pad, ty, minf(full, r.w - 2 * pad), ui_line_height(size)), RGBA(50, 90, 20, 255));
    ui_text(r.x + pad - scroll, ty, size, FONT_REGULAR, C_TEXT, buf);
    if (fmodf(U.blink, 1) < 0.6f) ui_rect(rect(r.x + pad + cx - scroll, ty, maxf(1, S(1)), ui_line_height(size)), C_BRIGHT);
  } else if (*buf) {
    ui_text_fit(r.x + pad, ty, r.w - 2 * pad, size, FONT_REGULAR, C_TEXT, buf);
  } else if (hint) {
    ui_text_fit(r.x + pad, ty, r.w - 2 * pad, size, FONT_REGULAR, C_FAINT, hint);
  }
  ui_clip_pop();
  return changed;
}

bool ui_multiline(Rect r, const char *ids, char *buf, int cap) {
  u32 id = ui_id(ids);
  bool over = ui_hovering(r);
  if (over && U.in.pressed[0]) {
    U.focus = id;
    U.cursor = (int)strlen(buf);
    U.select_all = false;
    U.blink = 0;
  } else if (U.focus == id && U.in.pressed[0] && !over) {
    U.focus = 0;
  }
  ui_rect(r, C_FIELD);
  ui_border(r, U.focus == id ? C_ACCENT : (over ? RGBA(60, 90, 40, 255) : C_LINE), maxf(1, S(1)));
  float size = S(11.5f), pad = S(6), lh = S(15);
  bool changed = false;
  if (U.focus == id) {
    changed = edit_apply(buf, cap, true);
    if (U.in.keys[KEY_ESCAPE]) U.focus = 0;
  }
  ui_clip_push(rect_inset(r, 1));
  ui_wrap(r.x + pad, r.y + pad, r.w - 2 * pad, size, FONT_REGULAR, C_TEXT, lh, buf);
  if (U.focus == id && fmodf(U.blink, 1) < 0.6f) {
    char pre[4096];
    int n = mini(U.cursor, (int)sizeof pre - 1);
    memcpy(pre, buf, (size_t)n);
    pre[n] = 0;
    float h = *pre ? ui_wrap_height(r.w - 2 * pad, size, FONT_REGULAR, lh, pre) : lh;
    const char *last = strrchr(pre, '\n');
    float cx = ui_text_width(size, FONT_REGULAR, 0, last ? last + 1 : pre);
    while (cx > r.w - 2 * pad) cx -= r.w - 2 * pad;
    if (n && pre[n - 1] == '\n') h += lh;
    ui_rect(rect(r.x + pad + cx, r.y + pad + h - lh, maxf(1, S(1)), lh), C_BRIGHT);
  }
  ui_clip_pop();
  return changed;
}

bool ui_drag(Rect r, const char *ids, double *v, double step, const char *unit) {
  u32 id = ui_id(ids);
  bool over = ui_hovering(r);
  bool changed = false;
  float size = S(12), pad = S(6);
  // Double-click, or focus handed on by Tab: type a value.
  if (U.focus == id) {
    ui_rect(r, C_FIELD);
    ui_border(r, C_ACCENT, maxf(1, S(1)));
    edit_apply(U.edit, sizeof U.edit, false);
    float ty = r.y + (r.h - ui_line_height(size)) / 2;
    ui_clip_push(rect_inset(r, 1));
    float full = ui_text_width(size, FONT_REGULAR, 0, U.edit);
    if (U.select_all && U.edit[0]) ui_rect(rect(r.x + pad, ty, minf(full, r.w - 2 * pad), ui_line_height(size)), RGBA(50, 90, 20, 255));
    ui_text(r.x + pad, ty, size, FONT_REGULAR, C_TEXT, U.edit);
    char pre[64];
    int n = mini(U.cursor, 63);
    memcpy(pre, U.edit, (size_t)n);
    pre[n] = 0;
    if (fmodf(U.blink, 1) < 0.6f)
      ui_rect(rect(r.x + pad + ui_text_width(size, FONT_REGULAR, 0, pre), ty, maxf(1, S(1)), ui_line_height(size)), C_BRIGHT);
    ui_clip_pop();
    bool commit = U.in.keys[KEY_ENTER] || U.in.keys[KEY_TAB] || (U.in.pressed[0] && !over);
    if (commit) {
      char *end;
      double parsed = strtod(U.edit, &end);
      if (end != U.edit && isfinite(parsed)) {
        changed = parsed != *v;
        *v = parsed;
      }
      U.focus = 0;
      if (U.in.keys[KEY_TAB]) {
        U.focus_next = true;
        U.last_focus_widget = id;
        U.focus = id;
        U.edit_id = id;
      }
    }
    if (U.in.keys[KEY_ESCAPE]) U.focus = 0;
    if (commit && U.in.keys[KEY_TAB]) {
      U.focus = 0;
      U.last_focus_widget = id;
    }
    return changed;
  }
  bool take = U.focus_next && U.last_focus_widget && U.last_focus_widget != id;
  if (take) {
    U.focus_next = false;
    U.focus = id;
    snprintf(U.edit, sizeof U.edit, "%.6g", *v);
    U.cursor = (int)strlen(U.edit);
    U.select_all = true;
  }
  if (over && U.in.double_click) {
    U.focus = id;
    U.active = 0;
    snprintf(U.edit, sizeof U.edit, "%.6g", *v);
    U.cursor = (int)strlen(U.edit);
    U.select_all = true;
    U.blink = 0;
  } else if (over && U.in.pressed[0] && !U.active) {
    U.active = id;
    U.focus = 0;
    U.drag_start = *v;
    U.drag_mx = U.in.mx;
  }
  if (U.active == id) {
    double nv = U.drag_start + (double)((U.in.mx - U.drag_mx) / S(4)) * step * (U.in.shift ? 0.1 : 1);
    // A clean number: rounded to the step's own digits.
    double k = step * (U.in.shift ? 0.1 : 1);
    nv = round(nv / k) * k;
    if (nv != *v) changed = true;
    *v = nv;
  }
  ui_rect(r, over || U.active == id ? C_HOVER : C_FIELD);
  ui_border(r, U.active == id ? C_ACCENT : C_LINE, maxf(1, S(1)));
  char text[64];
  if (unit && *unit) snprintf(text, sizeof text, "%.5g %s", *v, unit);
  else snprintf(text, sizeof text, "%.5g", *v);
  ui_text_fit(r.x + pad, r.y + (r.h - ui_line_height(size)) / 2, r.w - 2 * pad, size, FONT_REGULAR, C_TEXT, text);
  return changed;
}

bool ui_dropdown(Rect r, const char *ids, int *sel, const char *const *options, int n) {
  u32 id = ui_id(ids);
  bool over = ui_hovering(r);
  bool open = U.open_popup == id;
  float size = S(12), pad = S(6);
  if (over && U.in.pressed[0]) {
    U.open_popup = open ? 0 : id;
    open = !open;
    U.focus = 0;
    U.active = id;
  }
  ui_rect(r, over || open ? C_HOVER : C_FIELD);
  ui_border(r, open ? C_ACCENT : C_LINE, maxf(1, S(1)));
  const char *cur = *sel >= 0 && *sel < n ? options[*sel] : "\xE2\x80\x94";
  ui_text_fit(r.x + pad, r.y + (r.h - ui_line_height(size)) / 2, r.w - 2 * pad - S(14), size, FONT_REGULAR, C_TEXT, cur);
  ui_text(r.x + r.w - S(14), r.y + (r.h - ui_line_height(size)) / 2, size, FONT_REGULAR, C_DIM, "\xE2\x80\xBA");
  bool changed = false;
  if (open) {
    float rowh = S(22), h = rowh * (float)n;
    float y = r.y + r.h;
    if (y + h > (float)U.in.h) y = maxf(r.y - h, 0);
    Rect pop = rect(r.x, y, r.w, minf(h, (float)U.in.h - y));
    ui_set_popup(pop);
    int prev = U.layer, prevclips = U.nclips;
    U.layer = 1;
    U.nclips = 0;
    ui_rect(pop, RGBA(6, 12, 8, 255));
    ui_border(pop, C_ACCENT, maxf(1, S(1)));
    for (int i = 0; i < n; i++) {
      Rect row = rect(pop.x + 1, y + rowh * (float)i, pop.w - 2, rowh);
      bool hit = rect_has(row, U.in.mx, U.in.my);
      if (hit) ui_rect(row, C_HOVER);
      ui_text_fit(row.x + pad, row.y + (rowh - ui_line_height(size)) / 2, row.w - 2 * pad, size, FONT_REGULAR, i == *sel ? C_BRIGHT : C_TEXT, options[i]);
      if (hit && U.in.pressed[0]) {
        changed = i != *sel;
        *sel = i;
        U.open_popup = 0;
        U.active = id;
      }
    }
    if (U.in.pressed[0] && !rect_has(pop, U.in.mx, U.in.my) && !over) U.open_popup = 0;
    if (U.in.keys[KEY_ESCAPE]) U.open_popup = 0;
    U.layer = prev;
    U.nclips = prevclips;
  }
  return changed;
}

Scroll ui_scroll_begin(Rect r, const char *ids) {
  u32 id = ui_id(ids);
  float *pos = ui_state(id);
  if (ui_hovering(r) && U.in.wheel != 0) *pos -= U.in.wheel * S(40);
  if (*pos < 0) *pos = 0;
  ui_clip_push(r);
  return (Scroll){r, r.y - *pos, pos, r.y};
}
void ui_scroll_end(Scroll *s, float content_bottom) {
  ui_clip_pop();
  float content = content_bottom - s->y;
  float max_pos = maxf(content - s->r.h, 0);
  if (*s->pos > max_pos) *s->pos = max_pos;
  if (content > s->r.h) {
    float t = S(3), h = s->r.h * s->r.h / content;
    float y = s->r.y + (*s->pos / maxf(max_pos, 1)) * (s->r.h - h);
    ui_rect(rect(s->r.x + s->r.w - t - 1, y, t, h), RGBA(70, 110, 40, 200));
  }
}
