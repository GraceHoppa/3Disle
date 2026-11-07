// What the editor's parts share. editor.c holds documents, undo, the window's frame and dialogs;
// ed_build.c the Build tab; ed_props.c its property forms; ed_animate.c the Animate tab.
#pragma once
#include "editor.h"
#include "gl.h"
#include "render.h"
#include "shot.h"
#include "stage.h"
#include "video.h"
#include "viewer.h"

typedef enum { TAB_BUILD, TAB_ANIMATE, TAB_VIEW } Tab;

typedef enum { SEL_DEVICE, SEL_LAYER, SEL_PART, SEL_LINK, SEL_WIRE, SEL_CONTROL, SEL_READOUT, SEL_TOUR, SEL_LEVEL } SelKind;
typedef struct {
  SelKind kind;
  int layer, index;
} Sel;

// Undo keeps whole documents as text. A group opens at the first change and closes once the mouse is
// up and no field has focus, so one drag or one typed name is one step.
typedef struct {
  char *text;
  Sel sel;
} Snap;
typedef struct {
  Snap *undo, *redo; // arr
  char *base;        // the document as of the last closed group
  bool open;
} History;
void hist_init(History *h, const char *text);
void hist_free(History *h);
void hist_changed(History *h, Sel sel); // something changed: opens a group if none is open
void hist_settle(History *h, const char *text);
const char *hist_undo(History *h, Sel *sel, Sel now); // the text to go back to, or NULL
const char *hist_redo(History *h, Sel *sel, Sel now);

typedef struct {
  char path[1024]; // empty: never saved
  AssemblySpec *spec;  // what is being edited
  AssemblySpec *built; // the copy the model points into, so edits never pull it out from under the renderer
  Model *model;
  History hist;
  char *saved;
  bool dirty, stale;
  int version;       // grows with every change the other tabs should see
  double built_at;
  Sel sel;
  Stage stage;
  bool framed;
  float explode;
  bool closed_layer[128];
  float tree_scroll;
} Doc;

// A tour file: its levels as a list, kept as JSON so everything it says survives saving.
typedef struct {
  char path[1024];
  Arena ar;
  Json *root;
  History hist;
  char *saved;
  bool dirty;
} TourDoc;

typedef enum { MODAL_NONE, MODAL_OPEN, MODAL_SAVE_AS, MODAL_CLOSE, MODAL_QUIT, MODAL_RENDER, MODAL_NEW_VIDEO } Modal;

typedef enum { PICK_NONE, PICK_WIRE_FROM, PICK_WIRE_TO, PICK_LINK_FROM, PICK_LINK_TO } PickFor;

typedef struct {
  Shot *shot;
  History hist;
  char *saved;
  bool dirty;
  ShotPlayer *player;
  bool stale;
  int doc_version;
  double t;
  bool playing;
  bool detached;
  Stage stage;
  float px_per_s;
  double left;     // the time at the timeline's left edge
  float row_scroll;
  Sel sel;         // layer: track; index: key (-1 for the whole track); kinds below
  bool sel_on;
  int sel_what;    // 0 key, 1 caption, 2 label, 3 scene
  // Dragging on the timeline.
  int drag;        // 0 none, 1 key, 2 block, 3 block's left edge, 4 block's right edge, 5 scrub, 6 scene edge
  double drag_t0, drag_a, drag_b;
  float drag_mx;
  // The last frame's picture, so an idle timeline can answer without re-reading.
  ShotFrame frame;
  // Adding tracks.
  int add_kind, add_part;
  // Rendering.
  Video *video;
  ShotPlayer *video_player;
  char out[1024];
  int r_w, r_h, r_fps, r_crf;
  float r_ss;
  bool r_grain;
  bool r_done, r_failed, r_cancelled;
  bool video_finished;
  bool press_pending;
  char r_msg[256];
} Anim;

struct Editor {
  char root[560];
  Doc **docs; // arr
  int cur;
  TourDoc *tour;
  Tab tab;
  const UiInput *in;
  UiInput in_copy;
  u32 fbo;
  double clock;

  u32 vfbo;
  int vw, vh;

  char status[512];
  double status_at;
  bool status_err;

  int hover_node, hover_copy;
  bool part_drag;
  int drag_node;
  Vec3 drag_p0, drag_delta;
  float drag_plane;
  bool orbit_pending;

  PickFor pick;
  bool reveal; // bring the selected row into sight in the tree

  Modal modal;
  bool modal_for_video;
  int close_doc;
  bool quit_now;
  char dir[1024];
  char file_field[1024];
  char **entries; // arr: names, folders first, ending in /
  float list_scroll;

  // The text of the field being typed in, when it can't be written through as it is typed.
  char fbuf[4096];
  char fbuf_part[200]; // the part last clicked in the Animate picture

  bool changed; // this frame's edits, to the current document
  Sel sel_before;
  Anim a;

  Viewer *viewer;
  int viewer_version;
  int viewer_doc;

  char shape_pick;
  int add_shape;
};

static inline Doc *ed_doc(const Editor *e) { return arr_len(e->docs) ? e->docs[e->cur] : NULL; }

// editor.c
void ed_status(Editor *e, bool err, const char *fmt, ...);
void ed_view_prepare(Editor *e, int w, int h);
void ed_view_blit(Editor *e, Rect r);
void ed_doc_changed(Editor *e);
void ed_sel_valid(Doc *d);
void ed_doc_rebuild(Editor *e, Doc *d);
bool ed_doc_save(Editor *e, Doc *d);
int ed_open_file(Editor *e, const char *path, char *err, size_t errlen);
void ed_modal_files(Editor *e, Modal m);
void ed_all_overrides(Editor *e, SpecOverride **out);
Rect ed_body(const Editor *e);
void ed_sweep_textures(Editor *e);

// ed_build.c
void build_frame(Editor *e);
void build_keys(Editor *e);
void ed_rename_everywhere(Editor *e, const char *from, const char *to);

// ed_props.c
void props_panel(Editor *e, Rect r);
void props_take_pick(Editor *e, Model *m, Hit h);
bool ed_tour_active(const Editor *e);
const char *ed_tour_level_file(const Editor *e, int i);
bool ed_tour_save(Editor *e);
void ed_tour_settle(Editor *e);
bool ed_tour_undo(Editor *e, bool redo);
void tour_add_level(Editor *e);
void tour_remove_level(Editor *e, int i);

// ed_animate.c
void anim_frame(Editor *e);
void anim_keys(Editor *e);
void anim_render_modal(Editor *e);
void anim_render_tick(Editor *e);
bool anim_rendering(const Editor *e);
void anim_free(Editor *e);
bool anim_open(Editor *e, const char *path, char *err, size_t errlen);
bool anim_save(Editor *e);
void anim_undo(Editor *e, bool redo);
void anim_settle(Editor *e);
void anim_release_models(Editor *e);
void anim_query(Editor *e, const char *what, char *out, size_t n, bool *ok);
