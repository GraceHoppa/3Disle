// An immediate-mode interface: every frame the app says what is on screen, and what it asks about
// the mouse is answered there and then. Near-black, one green, mono type, square edges.
#pragma once
#include "base.h"

typedef struct {
  float x, y, w, h;
} Rect;
static inline Rect rect(float x, float y, float w, float h) { return (Rect){x, y, w, h}; }
static inline bool rect_has(Rect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
static inline Rect rect_inset(Rect r, float d) { return rect(r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d); }

typedef enum {
  KEY_NONE, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_ENTER, KEY_ESCAPE, KEY_BACKSPACE, KEY_DELETE, KEY_HOME, KEY_END,
  KEY_TAB, KEY_SPACE, KEY_A, KEY_C, KEY_D, KEY_E, KEY_G, KEY_K, KEY_L, KEY_N, KEY_O, KEY_R, KEY_S, KEY_V, KEY_X, KEY_Y, KEY_Z,
  KEY_COUNT
} UiKey;

typedef struct {
  int w, h;       // pixels
  float scale;    // interface scale, 1 at normal density
  float mx, my;   // pixels
  bool down[3];   // left, right, middle
  bool pressed[3], released[3];
  float wheel;
  bool keys[KEY_COUNT]; // pressed this frame
  bool ctrl, shift;
  const char *text; // UTF-8 typed this frame, or NULL
  float dt;
  bool double_click;
} UiInput;

#define RGBA(r, g, b, a) ((u32)(r) | (u32)(g) << 8 | (u32)(b) << 16 | (u32)(a) << 24)
#define C_ACCENT RGBA(0x76, 0xb9, 0x00, 255)
#define C_BRIGHT RGBA(0x9c, 0xff, 0x3a, 255)
#define C_TEXT RGBA(0xe8, 0xee, 0xe6, 255)
#define C_DIM RGBA(0x9a, 0xa5, 0x9c, 255)
#define C_FAINT RGBA(0x5e, 0x69, 0x60, 255)
#define C_LINE RGBA(0x24, 0x38, 0x22, 255)
#define C_PANEL_TOP RGBA(4, 10, 6, 214)
#define C_PANEL_BOT RGBA(4, 10, 6, 176)
#define C_FIELD RGBA(10, 18, 12, 255)
#define C_HOVER RGBA(24, 40, 20, 255)
#define C_ERROR RGBA(0xff, 0x6b, 0x5a, 255)
#define C_BG RGBA(4, 7, 10, 255)
static inline u32 c_alpha(u32 c, float a) { return (c & 0xffffff) | (u32)(clampf(a, 0, 1) * (float)(c >> 24)) << 24; }

typedef enum { FONT_REGULAR, FONT_BOLD } Font;

void ui_init(void);
void ui_begin(const UiInput *in);
void ui_render(u32 fbo); // draws everything said this frame
const UiInput *ui_input(void);
#define S(v) ((v)*ui_input()->scale)

// Drawing.
void ui_rect(Rect r, u32 c);
void ui_rect_grad(Rect r, u32 top, u32 bottom);
void ui_border(Rect r, u32 c, float t);
void ui_line(float x0, float y0, float x1, float y1, float w, u32 c);
void ui_circle(float cx, float cy, float r, u32 c);
void ui_panel(Rect r);
// x, y is the top-left of the line box. Returns the width drawn.
float ui_text(float x, float y, float size, Font f, u32 c, const char *s);
float ui_text_sp(float x, float y, float size, Font f, u32 c, float spacing, const char *s);
float ui_text_width(float size, Font f, float spacing, const char *s);
float ui_line_height(float size);
float ui_wrap(float x, float y, float w, float size, Font f, u32 c, float lh, const char *s); // returns the height
float ui_wrap_height(float w, float size, Font f, float lh, const char *s);
// Kept to `w`, ending in an ellipsis when it had to be cut.
void ui_text_fit(float x, float y, float w, float size, Font f, u32 c, const char *s);
void ui_clip_push(Rect r);
void ui_clip_pop(void);
void ui_set_layer(int layer); // 0 base, 1 popups on top

// Interaction. Ids come from strings, within the current scope.
u32 ui_id(const char *s);
void ui_push_id(int v);
void ui_pop_id(void);
void ui_block(Rect r);             // this area is interface: the 3D view under it gets no mouse
bool ui_wants_mouse(void);         // the pointer is over interface, or dragging a widget
bool ui_wants_keyboard(void);      // a text field has focus
bool ui_hovering(Rect r);          // false when a popup covers it
void ui_set_popup(Rect r);         // keeps widgets below from answering inside r

enum { BTN_ON = 1, BTN_DISABLED = 2, BTN_QUIET = 4, BTN_LEFT = 8 };
bool ui_button(Rect r, const char *label, int flags);
bool ui_slider(Rect r, const char *id, float *v, float mn, float mx);
// The value as text, in place: drag sideways to change it, double-click to type.
bool ui_drag(Rect r, const char *id, double *v, double step, const char *unit);
bool ui_text_field(Rect r, const char *id, char *buf, int cap, const char *hint);
bool ui_multiline(Rect r, const char *id, char *buf, int cap);
// A pick from a list, its options opening on the popup layer. Returns true when it changes.
bool ui_dropdown(Rect r, const char *id, int *sel, const char *const *options, int n);
void ui_unfocus(void);
void ui_focus_next(void);

// Scrolling panels: a clipped area with a position kept per id.
typedef struct {
  Rect r;
  float y;    // where content starts now
  float *pos;
  float top;
} Scroll;
Scroll ui_scroll_begin(Rect r, const char *id);
void ui_scroll_end(Scroll *s, float content_bottom);

// Small persistent values, by id.
float *ui_state(u32 id);
