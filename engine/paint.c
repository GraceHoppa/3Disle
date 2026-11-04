#include "paint.h"
#include "fonts_data.h"
#include "stb_truetype.h"

const u8 *font_regular(void) { return ASSET_JetBrainsMono_Regular_ttf; }
const u8 *font_bold(void) { return ASSET_JetBrainsMono_Bold_ttf; }

typedef struct {
  float r, g, b, a; // r g b 0..255, a 0..1
} Col;

typedef struct {
  int w, h;
  u8 *px;
} Canvas;

static inline void blend(u8 *d, Col c, float cov) {
  float a = c.a * cov;
  if (a <= 0) return;
  // Straight alpha source-over on an opaque or transparent canvas.
  float da = d[3] / 255.0f, oa = a + da * (1 - a);
  if (oa <= 0) return;
  float k = a / oa;
  d[0] = (u8)(d[0] + (c.r - d[0]) * k + 0.5f);
  d[1] = (u8)(d[1] + (c.g - d[1]) * k + 0.5f);
  d[2] = (u8)(d[2] + (c.b - d[2]) * k + 0.5f);
  d[3] = (u8)(oa * 255 + 0.5f);
}

static Canvas canvas_new(int w, int h) {
  Canvas c = {w, h, calloc((size_t)w * (size_t)h, 4)};
  if (!c.px) abort();
  return c;
}

static void fill_rect(Canvas *c, float x, float y, float w, float h, Col col) {
  float x1 = x + w, y1 = y + h;
  int ix0 = maxi((int)floorf(x), 0), iy0 = maxi((int)floorf(y), 0);
  int ix1 = mini((int)ceilf(x1), c->w), iy1 = mini((int)ceilf(y1), c->h);
  for (int py = iy0; py < iy1; py++) {
    float cy = minf((float)py + 1, y1) - maxf((float)py, y);
    if (cy <= 0) continue;
    for (int px = ix0; px < ix1; px++) {
      float cx = minf((float)px + 1, x1) - maxf((float)px, x);
      if (cx > 0) blend(c->px + 4 * (py * c->w + px), col, cx * cy);
    }
  }
}

static void fill_circle(Canvas *c, float cx, float cy, float r, Col col) {
  int ix0 = maxi((int)floorf(cx - r - 1), 0), iy0 = maxi((int)floorf(cy - r - 1), 0);
  int ix1 = mini((int)ceilf(cx + r + 1), c->w), iy1 = mini((int)ceilf(cy + r + 1), c->h);
  for (int py = iy0; py < iy1; py++)
    for (int px = ix0; px < ix1; px++) {
      float d = hypotf((float)px + 0.5f - cx, (float)py + 0.5f - cy);
      float cov = clampf(r - d + 0.5f, 0, 1);
      if (cov > 0) blend(c->px + 4 * (py * c->w + px), col, cov);
    }
}

typedef enum { CAP_ROUND, CAP_BUTT, CAP_SQUARE } Cap;

// A whole polyline blended once: overlapping segments don't darken the joints. Each row only
// looks at the pixels the capsules can reach, so long diagonals stay cheap.
static void stroke(Canvas *c, const Vec2 *pts, int n, float width, Cap cap, Col col) {
  if (n < 2) return;
  float r = width / 2;
  float reach = r + 1.5f + (cap == CAP_SQUARE ? r : 0);
  float ymin = INFINITY, ymax = -INFINITY;
  for (int i = 0; i < n; i++) ymin = minf(ymin, pts[i].y), ymax = maxf(ymax, pts[i].y);
  int y0 = maxi((int)floorf(ymin - reach), 0), y1 = mini((int)ceilf(ymax + reach), c->h);
  for (int py = y0; py < y1; py++) {
    float yc = (float)py + 0.5f;
    float xa = INFINITY, xb = -INFINITY;
    for (int s = 0; s + 1 < n; s++) {
      Vec2 a = pts[s], b = pts[s + 1];
      float lo = minf(a.y, b.y) - reach, hi = maxf(a.y, b.y) + reach;
      if (yc < lo || yc > hi) continue;
      float dy = b.y - a.y, dx = b.x - a.x;
      float sa, sb;
      if (fabsf(dy) < 1e-4f) {
        sa = minf(a.x, b.x) - reach, sb = maxf(a.x, b.x) + reach;
      } else {
        float len = hypotf(dx, dy);
        float mid = a.x + (yc - a.y) * dx / dy;
        float half = reach * len / fabsf(dy);
        sa = mid - half, sb = mid + half;
        float cl = minf(a.x, b.x) - reach, ch = maxf(a.x, b.x) + reach;
        sa = maxf(sa, cl), sb = minf(sb, ch);
      }
      xa = minf(xa, sa), xb = maxf(xb, sb);
    }
    if (xa > xb) continue;
    int ix0 = maxi((int)floorf(xa), 0), ix1 = mini((int)ceilf(xb), c->w);
    for (int px = ix0; px < ix1; px++) {
      float xc = (float)px + 0.5f, best = INFINITY;
      for (int s = 0; s + 1 < n; s++) {
        Vec2 a = pts[s], b = pts[s + 1];
        float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        float t = l2 > 0 ? ((xc - a.x) * dx + (yc - a.y) * dy) / l2 : 0, ext = 0;
        if (cap != CAP_ROUND) {
          // Beyond an end: distance along the line counts as outside the cap.
          float l = sqrtf(l2), over = 0;
          if (t < 0 && s == 0) over = -t * l;
          if (t > 1 && s == n - 2) over = (t - 1) * l;
          ext = cap == CAP_BUTT ? over : maxf(0, over - r);
        }
        float k = clampf(t, 0, 1);
        float ex = a.x + dx * k - xc, ey = a.y + dy * k - yc;
        // The line's own side distance plus how far past the cap, whichever is larger.
        float side = cap == CAP_ROUND ? sqrtf(ex * ex + ey * ey) : fabsf((xc - a.x) * dy - (yc - a.y) * dx) / maxf(sqrtf(l2), 1e-5f);
        if (cap != CAP_ROUND && ext > 0) side = maxf(side, r + ext);
        best = minf(best, side);
      }
      float cov = clampf(r - best + 0.5f, 0, 1);
      if (width < 1) cov *= width > 0 ? minf(1, width + 0.15f) : 0;
      if (cov > 0) blend(c->px + 4 * (py * c->w + px), col, cov);
    }
  }
}

static void line(Canvas *c, float ax, float ay, float bx, float by, float w, Cap cap, Col col) {
  Vec2 p[2] = {{ax, ay}, {bx, by}};
  stroke(c, p, 2, w, cap, col);
}

// ------------------------------------------------------------------ text
static stbtt_fontinfo g_font;
static bool g_font_ready;

static void font_init(void) {
  if (g_font_ready) return;
  stbtt_InitFont(&g_font, font_regular(), 0);
  g_font_ready = true;
}

static u32 next_cp(const char **s) {
  const unsigned char *c = (const unsigned char *)*s;
  u32 cp;
  if (*c < 0x80) cp = *c, *s += 1;
  else if (*c < 0xE0) cp = (u32)(c[0] & 31) << 6 | (c[1] & 63), *s += 2;
  else if (*c < 0xF0) cp = (u32)(c[0] & 15) << 12 | (u32)(c[1] & 63) << 6 | (c[2] & 63), *s += 3;
  else cp = (u32)(c[0] & 7) << 18 | (u32)(c[1] & 63) << 12 | (u32)(c[2] & 63) << 6 | (c[3] & 63), *s += 4;
  return cp;
}

// Baseline text; `mid` centres it on (x, y) instead, `center` centres it horizontally.
static void text_at(Canvas *c, const char *s, float x, float y, float px, Col col, bool center, bool mid) {
  font_init();
  float scale = stbtt_ScaleForPixelHeight(&g_font, px);
  int asc, desc, gap;
  stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
  float base = mid ? y + (float)(asc + desc) / 2 * scale : y;
  float width = 0;
  for (const char *t = s; *t;) {
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&g_font, (int)next_cp(&t), &adv, &lsb);
    width += (float)adv * scale;
  }
  float pen = center ? x - width / 2 : x;
  for (const char *t = s; *t;) {
    int cp = (int)next_cp(&t), adv, lsb, x0, y0, x1, y1;
    stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);
    stbtt_GetCodepointBitmapBox(&g_font, cp, scale, scale, &x0, &y0, &x1, &y1);
    int gw = x1 - x0, gh = y1 - y0;
    if (gw > 0 && gh > 0) {
      u8 *bmp = malloc((size_t)gw * (size_t)gh);
      stbtt_MakeCodepointBitmap(&g_font, bmp, gw, gh, gw, scale, scale, cp);
      int ox = (int)lroundf(pen) + x0, oy = (int)lroundf(base) + y0;
      for (int j = 0; j < gh; j++)
        for (int i = 0; i < gw; i++) {
          int px_ = ox + i, py_ = oy + j;
          if (px_ < 0 || py_ < 0 || px_ >= c->w || py_ >= c->h) continue;
          if (bmp[j * gw + i]) blend(c->px + 4 * (py_ * c->w + px_), col, bmp[j * gw + i] / 255.0f);
        }
      free(bmp);
    }
    pen += (float)adv * scale;
  }
}

// ------------------------------------------------------------------ colours
static Col rgba(float r, float g, float b, float a) { return (Col){r, g, b, a}; }
static Col hexcol(const char *hex) {
  Vec3 c = hex_srgb(hex);
  return rgba(c.x * 255, c.y * 255, c.z * 255, 1);
}
// `hex` moved toward white (k > 0) or black (k < 0).
static Col shade(const char *hex, float k, float a) {
  Vec3 c = hex_srgb(hex);
  float ch[3] = {c.x * 255, c.y * 255, c.z * 255};
  for (int i = 0; i < 3; i++) ch[i] = roundf(k >= 0 ? ch[i] + (255 - ch[i]) * k : ch[i] * (1 + k));
  return rgba(ch[0], ch[1], ch[2], a);
}

static Col mix_col(Col a, Col b, float t) {
  return rgba(lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t), lerpf(a.a, b.a, t));
}

// A linear gradient across the whole canvas, from (x0,y0) to (x1,y1), through evenly listed stops.
static void fill_gradient(Canvas *c, float x0, float y0, float x1, float y1, const float *stops, const Col *cols, int n, bool screen) {
  float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
  for (int py = 0; py < c->h; py++)
    for (int px = 0; px < c->w; px++) {
      float t = l2 > 0 ? (((float)px + 0.5f - x0) * dx + ((float)py + 0.5f - y0) * dy) / l2 : 0;
      t = clampf(t, 0, 1);
      int k = 0;
      while (k + 1 < n - 1 && t > stops[k + 1]) k++;
      float seg = stops[k + 1] - stops[k];
      Col col = mix_col(cols[k], cols[k + 1], seg > 0 ? clampf((t - stops[k]) / seg, 0, 1) : 0);
      u8 *d = c->px + 4 * (py * c->w + px);
      if (screen) {
        // screen composite: 1 - (1-s)(1-d), weighted by the source alpha.
        for (int i = 0; i < 3; i++) {
          float s = (i == 0 ? col.r : i == 1 ? col.g : col.b) / 255, v = d[i] / 255.0f;
          float o = 1 - (1 - s) * (1 - v);
          d[i] = (u8)(lerpf(v, o, col.a) * 255 + 0.5f);
        }
      } else {
        blend(d, col, 1);
      }
    }
}

void surface_size(float width, float depth, float pxmm, int *w, int *h) {
  const float MAX_PX = 2048, MIN_PX = 64;
  float w0 = fabsf(width) * pxmm, h0 = fabsf(depth) * pxmm, l = maxf(w0, h0);
  if (!(l > 0) || !isfinite(l)) {
    *w = *h = (int)MIN_PX;
    return;
  }
  float target = l > MAX_PX ? MAX_PX : l < MIN_PX ? MIN_PX : 0;
  float scale = target != 0 ? target / l : 1;
  if (w0 == h0) {
    int side = target != 0 ? (int)target : maxi(1, (int)lroundf(w0));
    *w = *h = side;
    return;
  }
  int pw = maxi(1, (int)lroundf(w0 * scale)), ph = maxi(1, (int)lroundf(h0 * scale));
  if (target != 0) {
    if (w0 > h0) pw = (int)target;
    else ph = (int)target;
  }
  *w = pw, *h = ph;
}

// ------------------------------------------------------------------ surfaces
static void paint_traces(Canvas *c, float boardW, float boardD, Rng *rng, const char *base) {
  int pw = c->w, ph = c->h;
  float s = boardW > 0 ? (float)pw / boardW : 1;
  fill_rect(c, 0, 0, (float)pw, (float)ph, hexcol(base ? base : "#120d0a"));
  float unit = maxf(boardW, boardD) / 60 * s;
  int bundles = mini(260, (int)lround(boardW * boardD * s * s / (unit * unit) / 14));
  for (int i = 0; i < bundles; i++) {
    int k = (int)floor(rng_next(rng) * 4) * 2;
    int turn = rng_next(rng) < 0.5 ? 1 : 7;
    float d1x = cosf((float)k * PI / 4), d1y = sinf((float)k * PI / 4);
    float d2x = cosf((float)((k + turn) % 8) * PI / 4), d2y = sinf((float)((k + turn) % 8) * PI / 4);
    int lanes = 2 + (int)floor(rng_next(rng) * 6);
    float pitch = unit * (0.32f + (float)rng_next(rng) * 0.2f);
    float l1 = unit * (2 + (float)rng_next(rng) * 6);
    float l2 = unit * (1 + (float)rng_next(rng) * 5);
    float x0 = (float)rng_next(rng) * (float)pw, y0 = (float)rng_next(rng) * (float)ph;
    float alpha = 0.55f + (float)rng_next(rng) * 0.4f;
    Col st = rgba(214, 128, 72, alpha), fl = rgba(240, 170, 110, alpha);
    float lw = maxf(1, unit * 0.09f);
    for (int l = 0; l < lanes; l++) {
      float ox = -d1y * pitch * (float)l, oy = d1x * pitch * (float)l;
      float bend = pitch * (float)l * (turn == 1 ? -0.41f : 0.41f);
      float bx = x0 + ox + d1x * (l1 + bend), by = y0 + oy + d1y * (l1 + bend);
      float ex = bx + d2x * l2, ey = by + d2y * l2;
      Vec2 p[3] = {{x0 + ox, y0 + oy}, {bx, by}, {ex, ey}};
      stroke(c, p, 3, lw, CAP_ROUND, st);
      fill_circle(c, ex, ey, maxf(1.2f, unit * 0.16f), fl);
    }
  }
}

static void paint_laminate(Canvas *c, Rng *rng) {
  int w = c->w, h = c->h;
  fill_rect(c, 0, 0, (float)w, (float)h, hexcol("#b9a873"));
  int step = maxi(3, (int)lround(mini(w, h) / 90.0));
  float lw = maxf(1, (float)step * 0.45f);
  for (int i = 0; i < w + h; i += step) {
    line(c, (float)i, 0, (float)i, (float)h, lw, CAP_BUTT, rgba(90, 76, 40, 0.08f + (float)rng_next(rng) * 0.08f));
    line(c, 0, (float)i, (float)w, (float)i, lw, CAP_BUTT, rgba(255, 245, 210, 0.05f + (float)rng_next(rng) * 0.07f));
  }
}

static void paint_pcb(Canvas *c, float boardW, float boardD, const Footprint *fp, int nfp, Rng *rng, const char *base) {
  int pw = c->w, ph = c->h;
  float sx = boardW > 0 ? (float)pw / boardW : 0, sz = boardD > 0 ? (float)ph / boardD : 0;
#define TOX(x) ((float)pw / 2 + (x) * sx)
#define TOY(z) ((float)ph / 2 + (z) * sz)
  Col g0 = base ? shade(base, 0.06f, 1) : hexcol("#0b3a1d");
  Col g1 = base ? shade(base, -0.15f, 1) : hexcol("#082b15");
  float stops[2] = {0, 1};
  Col cols[2] = {g0, g1};
  fill_gradient(c, 0, 0, (float)pw, (float)ph, stops, cols, 2, false);
  float area = boardW * boardD;
  int traces = area > 0 ? mini(1200, (int)lround(300.0 * area / (240 * 110))) : 0;
  for (int i = 0; i < traces; i++) {
    float x = (float)(rng_next(rng) - 0.5) * boardW, z = (float)(rng_next(rng) - 0.5) * boardD;
    float tx = x + (float)(rng_next(rng) - 0.5) * boardW * (80.f / 240), tz = z + (float)(rng_next(rng) - 0.5) * boardD * (50.f / 110);
    Vec2 route[4];
    route45(x, z, tx, tz, (float)rng_next(rng), route);
    float a = 0.25f + (float)rng_next(rng) * 0.3f;
    Col st = base ? shade(base, 0.22f, a) : rgba(60, 140, 70, a);
    float lw = sx * (0.2f + (float)rng_next(rng) * 0.35f);
    Vec2 p[4];
    for (int k = 0; k < 4; k++) p[k] = v2(TOX(route[k].x), TOY(route[k].y));
    stroke(c, p, 4, lw, CAP_ROUND, st);
  }
  int vias = area > 0 ? mini(3600, (int)lround(900.0 * area / (240 * 110))) : 0;
  for (int i = 0; i < vias; i++) {
    float cx = TOX((float)(rng_next(rng) - 0.5) * boardW), cy = TOY((float)(rng_next(rng) - 0.5) * boardD);
    fill_circle(c, cx, cy, sx * 0.35f, rgba(190, 170, 90, 0.55f));
    fill_circle(c, cx, cy, sx * 0.15f, hexcol("#06200f"));
  }
  float pad = 0.4f;
  Col silk = rgba(235, 240, 230, 0.75f), ink = rgba(235, 240, 230, 0.8f);
  float lw = sx * 0.25f;
  for (int i = 0; i < nfp; i++) {
    const Footprint *f = &fp[i];
    if (!f->silk) continue;
    int chars = 0;
    for (const char *t = f->name; *t;) next_cp(&t), chars++;
    float em = minf(2.6f, maxf(0.5f, (f->w + pad * 2) / ((float)maxi(chars, 1) * 0.62f)));
    float x = TOX(f->x - f->w / 2 - pad), y = TOY(f->z - f->d / 2 - pad);
    float rw = (f->w + pad * 2) * sx, rh = (f->d + pad * 2) * sz;
    Vec2 box[5] = {{x, y}, {x + rw, y}, {x + rw, y + rh}, {x, y + rh}, {x, y}};
    stroke(c, box, 5, lw, CAP_BUTT, silk);
    if (*f->name) text_at(c, f->name, x, y - sx * 0.8f, maxf(1, roundf(sx * em)), ink, false, false);
  }
}

static void paint_carbon(Canvas *c, Rng *rng) {
  int w = c->w, h = c->h;
  fill_rect(c, 0, 0, (float)w, (float)h, hexcol("#121418"));
  float tow = (float)maxi(5, mini(22, (int)lround(mini(w, h) / 28.0)));
  float phase = (float)rng_next(rng) * tow * 2;
  float span = (float)(w + h);
  float lw = tow - 1;
  for (float i = -(float)h + phase; i < (float)w + phase; i += tow) {
    int family = (int)floorf((i - phase) / tow / 2) % 2;
    line(c, i, 0, i + (float)h, (float)h, lw, CAP_SQUARE, hexcol(family == 0 ? "#3a414b" : "#1e2228"));
  }
  lw = maxf(1, (tow - 1) * 0.55f);
  for (float i = phase; i < span + phase; i += tow) {
    int over = (int)floorf((i - phase) / tow / 2) % 2;
    Col col = over == 0 ? rgba(92, 98, 108, 0.55f) : rgba(8, 9, 11, 0.55f);
    line(c, (float)w - i, 0, (float)w - i - (float)h, (float)h, lw, CAP_SQUARE, col);
  }
  Col hi = rgba(210, 214, 220, 0.14f);
  for (float i = -(float)h + phase + tow * 0.28f; i < (float)w + phase; i += tow)
    line(c, i, 0, i + (float)h, (float)h, 1, CAP_SQUARE, hi);
}

static void paint_brushed(Canvas *c, Rng *rng, const char *base) {
  int w = c->w, h = c->h;
  float stops[3] = {0, 0.5f, 1};
  Col cols[3] = {base ? shade(base, 0.12f, 1) : hexcol("#e8ecf1"), base ? hexcol(base) : hexcol("#d2d7de"),
                 base ? shade(base, -0.12f, 1) : hexcol("#c4cad2")};
  fill_gradient(c, 0, 0, 0, (float)h, stops, cols, 3, false);
  int n = mini(640, maxi(48, (int)lround((double)h * 0.55)));
  for (int i = 0; i < n; i++) {
    float y = (float)rng_next(rng) * (float)h, x0 = (float)rng_next(rng) * (float)w;
    float len = (0.1f + (float)rng_next(rng) * 0.55f) * (float)w;
    int v = (int)floor((rng_next(rng) > 0.55 ? 198 : 108) + rng_next(rng) * 42);
    float a = 0.08f + (float)rng_next(rng) * 0.22f;
    float lw = 0.45f + (float)rng_next(rng) * 0.7f;
    line(c, x0, y, x0 + len, y, lw, CAP_BUTT, rgba((float)v, (float)v, (float)mini(255, v + 5), a));
  }
}

u8 *paint_surface(const char *kind, int w, int h, float pxmm, float width, float depth, const Footprint *prints, int nprints, u32 seed, const char *base) {
  (void)pxmm;
  Canvas c = canvas_new(w, h);
  Rng rng = rng_new(seed);
  float bw = fabsf(width), bd = fabsf(depth);
  if (!strcmp(kind, "pcb")) paint_pcb(&c, bw, bd, prints, nprints, &rng, base);
  else if (!strcmp(kind, "carbon")) paint_carbon(&c, &rng);
  else if (!strcmp(kind, "traces")) paint_traces(&c, bw, bd, &rng, base);
  else if (!strcmp(kind, "laminate")) paint_laminate(&c, &rng);
  else paint_brushed(&c, &rng, base);
  return c.px;
}

// ------------------------------------------------------------------ silicon
u8 *paint_pattern(const char *kind, int wi, int hi, const char *base_in, int seed, int cx, int cy) {
  Canvas c = canvas_new(wi, hi);
  const char *base = base_in ? base_in : "#3a4a3a";
  Rng rng = rng_new((u32)seed);
  float w = (float)wi, h = (float)hi;
  fill_rect(&c, 0, 0, w, h, shade(base, -0.35f, 1));
  float gut = maxf(1, minf(w, h) * 0.012f);
  if (!strcmp(kind, "sm-grid")) {
    float tw = (w - gut * (float)(cx + 1)) / (float)cx, th = (h - gut * (float)(cy + 1)) / (float)cy;
    for (int i = 0; i < cx; i++)
      for (int j = 0; j < cy; j++) {
        float x = gut + (float)i * (tw + gut), y = gut + (float)j * (th + gut);
        fill_rect(&c, x, y, tw, th, shade(base, -0.05f + (float)rng_next(&rng) * 0.08f, 1));
        fill_rect(&c, x + tw * 0.06f, y + th * 0.08f, tw * 0.4f, th * 0.5f, shade(base, 0.25f, 1));
        for (int q = 0; q < 4; q++)
          fill_rect(&c, x + tw * (0.52f + (float)(q % 2) * 0.22f), y + th * (0.08f + (float)(q / 2) * 0.27f), tw * 0.19f, th * 0.23f,
                    shade(base, 0.08f + (float)(q % 2) * 0.06f, 1));
        fill_rect(&c, x + tw * 0.06f, y + th * 0.66f, tw * 0.88f, th * 0.26f, shade(base, -0.18f, 1));
        for (int k = 0; k < 6; k++) {
          float yy = y + th * 0.68f + (float)k * th * 0.22f / 6;
          line(&c, x + tw * 0.08f, yy, x + tw * 0.92f, yy, 1, CAP_BUTT, shade(base, 0.35f, 0.35f));
        }
      }
  } else if (!strcmp(kind, "sram")) {
    int bx = maxi(2, (int)lroundf(w / maxf(h, 1)) * 2), by = 2;
    float bw = (w - gut * (float)(bx + 1)) / (float)bx, bh = (h - gut * (float)(by + 1)) / (float)by;
    for (int i = 0; i < bx; i++)
      for (int j = 0; j < by; j++) {
        float x = gut + (float)i * (bw + gut), y = gut + (float)j * (bh + gut);
        fill_rect(&c, x, y, bw, bh, shade(base, 0.05f, 1));
        float pitch = maxf(2, minf(bw, bh) / 14);
        Col cell = shade(base, 0.3f, 0.55f);
        for (float yy = y + pitch * 0.5f; yy < y + bh - pitch * 0.3f; yy += pitch)
          for (float xx = x + pitch * 0.5f; xx < x + bw - pitch * 0.3f; xx += pitch)
            fill_rect(&c, xx, yy, pitch * 0.55f, pitch * 0.4f, cell);
        fill_rect(&c, x, y + bh * 0.46f, bw, bh * 0.08f, shade(base, -0.2f, 1));
      }
  } else if (!strcmp(kind, "phy")) {
    bool along = w >= h;
    const int n = 24;
    for (int i = 0; i < n; i++) {
      Col col = shade(base, i % 3 == 0 ? 0.3f : 0.05f + (float)rng_next(&rng) * 0.1f, 1);
      float t = (float)i / (float)n;
      if (along) fill_rect(&c, gut + t * (w - gut * 2), gut, (w - gut * 2) / (float)n - gut * 0.5f, h - gut * 2, col);
      else fill_rect(&c, gut, gut + t * (h - gut * 2), w - gut * 2, (h - gut * 2) / (float)n - gut * 0.5f, col);
    }
  } else {
    float row = maxf(2, h / 40);
    for (float y = gut; y < h - row; y += row * 1.15f) {
      float x = gut;
      while (x < w - gut) {
        float cw = row * (0.4f + (float)rng_next(&rng) * 2.2f);
        Col col = shade(base, -0.05f + (float)rng_next(&rng) * 0.3f, 1);
        fill_rect(&c, x, y, minf(cw, w - gut - x), row, col);
        x += cw + row * 0.15f;
      }
    }
  }
  float stops[4] = {0, 0.35f, 0.65f, 1};
  Col sheen[4] = {rgba(140, 90, 255, 0.22f), rgba(60, 170, 255, 0.16f), rgba(90, 255, 160, 0.14f), rgba(255, 190, 80, 0.2f)};
  fill_gradient(&c, 0, 0, w, h, stops, sheen, 4, true);
  return c.px;
}

u8 *paint_marking(const char *text_in, int wi, int hi, bool exact) {
  Canvas c = canvas_new(wi, hi);
  float cw = (float)wi, ch = (float)hi;
  fill_circle(&c, ch * 0.12f, ch * 0.12f, minf(cw, ch) * 0.05f, rgba(200, 206, 212, 0.55f));
  // A part's name loses its trailing "(...)"; an explicit marking stays as written.
  char buf[256];
  snprintf(buf, sizeof buf, "%s", text_in);
  if (!exact) {
    char *paren = strrchr(buf, '(');
    size_t n = strlen(buf);
    if (paren && n && buf[n - 1] == ')') {
      *paren = 0;
      for (n = strlen(buf); n && buf[n - 1] == ' '; n--) buf[n - 1] = 0;
    }
  }
  // Greedy wrap at 12 characters.
  char lines[8][64];
  int nl = 0;
  char *save = buf;
  for (char *word = save; word && *word && nl <= 8;) {
    char *sp = strpbrk(word, " \t\n");
    if (sp) *sp = 0;
    if (*word) {
      if (nl && strlen(lines[nl - 1]) + 1 + strlen(word) <= 12) {
        strncat(lines[nl - 1], " ", 63 - strlen(lines[nl - 1]));
        strncat(lines[nl - 1], word, 63 - strlen(lines[nl - 1]));
      } else if (nl < 8) {
        snprintf(lines[nl++], 64, "%s", word);
      }
    }
    word = sp ? sp + 1 : NULL;
  }
  int longest = 1;
  for (int i = 0; i < nl; i++) longest = maxi(longest, (int)strlen(lines[i]));
  float size = minf(ch / ((float)nl + 1.5f), cw * 0.85f / ((float)longest * 0.62f));
  Col col = rgba(200, 206, 212, 0.7f);
  // This canvas is a decal: transparent elsewhere. Text keeps its own alpha.
  float px = roundf(size);
  if (px < 1) px = 1;
  for (int i = 0; i < nl; i++)
    text_at(&c, lines[i], cw / 2, ch / 2 + ((float)i - (float)(nl - 1) / 2) * size * 1.15f, px, col, true, true);
  // Keep the colour under fully transparent pixels from going black at the filtered edges.
  for (int i = 0; i < wi * hi; i++)
    if (!c.px[i * 4 + 3]) c.px[i * 4] = 200, c.px[i * 4 + 1] = 206, c.px[i * 4 + 2] = 212;
  return c.px;
}
