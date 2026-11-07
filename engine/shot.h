// A video as data: scenes (device files on a time line), tracks of keys that move the camera and
// the parts, labels and captions. Everything is a pure function of time: the same t always gives
// the same frame, however it got there, so a video never depends on how fast it was rendered.
#pragma once
#include "model.h"

typedef enum { EASE_SMOOTH, EASE_LINEAR, EASE_HOLD, EASE_OUT, EASE_IN, EASE_COUNT } Ease;
extern const char *const EASE_NAMES[EASE_COUNT];

typedef enum {
  TRACK_CAMERA,  // pos, target, fov
  TRACK_EXPLODE, // Disassemble 0..1
  TRACK_FADE,    // 0 clear, 1 black
  TRACK_FLOW,    // how fast things flow, 1 is realtime
  TRACK_CONTROL, // `name` is a control id; the value it takes
  TRACK_OFFSET,  // `name` is a part; moved by [x, y, z] mm
  TRACK_OPACITY, // `name` is a part; 1 solid, 0 gone
  TRACK_GLOW,    // `name` is a part; lights up
  TRACK_KIND_COUNT
} TrackKind;
extern const char *const TRACK_NAMES[TRACK_KIND_COUNT];

typedef struct {
  double t;
  double v[3];      // scalar in v[0]; camera position; part offset
  double target[3]; // camera only
  double fov;       // camera only
  Ease ease;        // how it travels from this key to the next
} Key;

typedef struct {
  TrackKind kind;
  int scene;        // -1 for fade and flow, which span the whole video
  const char *name;
  Key *keys;        // arr, in time order
} Track;

typedef struct {
  const char *file;
  double start, end;
} ShotScene;

typedef struct {
  double t0, t1;
  int scene;
  const char *part;
  const char *text; // NULL: the part's own name
} ShotLabel;

typedef struct {
  double t0, t1;
  const char *scale, *title, *text, *caption;
} ShotCaption;

typedef struct {
  Arena ar;
  const char *name;
  int fps, w, h;
  double duration;
  ShotScene *scenes;     // arr
  Track *tracks;         // arr
  ShotLabel *labels;     // arr
  ShotCaption *captions; // arr
  char path[512];
} Shot;

Shot *shot_parse(const char *text, const char *path, char *err, size_t errlen);
Shot *shot_load(const char *path, char *err, size_t errlen);
Shot *shot_new(const char *name, const char *path);
void shot_free(Shot *s);
char *shot_write(const Shot *s); // free() the result
bool shot_save(const Shot *s, const char *path);
const char *shot_str(Shot *s, const char *text);

int shot_scene_at(const Shot *s, double t);
Track *shot_track(Shot *s, TrackKind kind, int scene, const char *name, bool create);
int track_set_key(Track *tr, Key k);   // adds, or replaces a key at the same moment; keeps order
double track_value(const Track *tr, double t, double fallback);
void track_vec3(const Track *tr, double t, double out[3]);
bool track_camera(const Track *tr, double t, Vec3 *pos, Vec3 *target, float *fov);
void shot_recompute_duration(Shot *s);
float ease_apply(Ease e, float k);

// Playing it: the scenes built, positioned at any moment.
typedef struct {
  Shot *shot;
  AssemblySpec **specs; // arr per scene
  Model **models;       // arr per scene
  int cur;
  double at;
  bool valid;
  char *errors;
} ShotPlayer;

typedef struct {
  Model *model;   // NULL when no scene covers this moment
  int scene;
  Vec3 cam_pos, cam_target;
  float fov, near_z, far_z;
  float fade;
} ShotFrame;

ShotPlayer *player_new(Shot *s, BuildOpts opts, char *err, size_t errlen);
// The same, with documents still open in the editor standing in for their files on disk.
typedef struct {
  const char *path;
  const AssemblySpec *spec;
} SpecOverride;
ShotPlayer *player_new_over(Shot *s, BuildOpts opts, const SpecOverride *over, int nover, char *err, size_t errlen);
void player_free(ShotPlayer *p);
// Rebuild one scene after its file changed.
void player_invalidate(ShotPlayer *p);
ShotFrame player_seek(ShotPlayer *p, double t, float aspect);
void default_camera(const Model *m, float aspect, float from, Vec3 *pos, Vec3 *target);
float fit_radius_distance(float radius, float fov_deg, float aspect);

// Write a whole video automatically from what the data already says. `path` is a device or a tour;
// a tour dives from level to level, a device comes apart and shows its biggest parts.
Shot *shot_auto(const char *path, const char *out_path, char *err, size_t errlen);
