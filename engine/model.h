// What a device becomes once built: a tree of named nodes with meshes and materials. No GL here,
// so the whole of it runs in tests. The renderer reads it; the viewer and timeline move it.
#pragma once
#include "mesh.h"
#include "spec.h"

typedef enum {
  MAT_STANDARD, // physically based, lit
  MAT_WIRE,     // lit insulation with what flows drawn on it
  MAT_FLOW,     // unlit trace with pulses running along it
  MAT_DECAL,    // unlit painted plane
} MatMode;

typedef struct Texture {
  char *key;
  int w, h;
  u8 *rgba; // freed once uploaded
  bool repeat;
  u32 gl;
  u32 last_used;
  bool marked;
} Texture;

typedef struct {
  MatMode mode;
  Vec3 color; // linear
  float metalness, roughness, opacity, clearcoat;
  bool transparent;
  Vec3 emissive; // linear
  float emissive_intensity;
  Texture *tex;
  float uv_scale;
  // Wires and flows.
  Vec3 glow;
  int wire_mode;
  float wire_phase, wire_length;
} Material;

enum {
  NF_ROOT = 1 << 0,
  NF_LAYER = 1 << 1,
  NF_PART = 1 << 2,
  NF_SLAB = 1 << 3,
  NF_SCATTER = 1 << 4,
  NF_WIRES = 1 << 5,
  NF_WIRE = 1 << 6,
  NF_FLOW = 1 << 7,
  NF_DECAL = 1 << 8,
  NF_HIDDEN = 1 << 9,
};

typedef struct {
  Vec3 p0;      // assembled position, model space
  int node;     // the part it is fixed to
  Vec3 local;   // in that node's space
  Vec3 last;    // where it was when last laid
} WireEnd;

typedef struct {
  WireEnd a, b;
  Vec3 *inner;  // arr: assembled inner points
  float *inner_t;
  int segments;
  float radius;
  int spec; // index into spec->wires
} WireState;

typedef struct Node {
  int parent, first, last, next;
  const char *name, *info, *category, *detail, *material_name;
  const KV *specs;
  u32 flags;

  // Where it is: home plus what is moving it now.
  Vec3 home;
  float rot0;       // built-in turn about y
  Vec3 off_spread;  // Disassemble
  Vec3 off_room;    // making room around a selection
  Vec3 off_anim;    // the timeline
  float spin_angle;
  Mat4 world;

  Mesh *mesh; // owned; NULL for a group
  Material mats[6];
  int nmats;
  Mat4 *inst; // arr; NULL for one copy
  float *inst_tint; // arr of 3 per copy, or NULL
  bool inst_dirty;
  u32 gl_inst;
  const char **names; // per-copy names (the spec's)

  // Sizes at assembly, in model space; `self` is this node's own geometry.
  Box self, box;

  const Value *spin;
  bool has_pulse;
  float pulse_phase;
  const Value *pulse_level;
  Vec3 pulse_color;
  const Value *level;

  // Animated values, advanced by model_tick.
  float flow_a, flow_b, level_now;
  float highlight; // hover glow 0..1
  float opacity;   // timeline ghosting, 1 is solid
  float glow_anim; // timeline glow
  Vec3 glow_anim_color;

  int wire; // into Model.wires, or -1
  int layer_index, part_index; // back to the spec, -1 when not applicable
} Node;

typedef struct {
  AssemblySpec *spec; // not owned
  Node *nodes;        // arr; parents always come before their children
  WireState *wires;   // arr
  int root, wiring;
  int *pieces;        // arr: top-level pieces that come apart (not the wiring)
  bool layered;       // stacked layers spread along y; else outward
  Box box;            // assembled
  Vec3 center;
  float radius;
  // Disassemble, computed once from the assembled boxes.
  float *spread_dir;  // arr: 3 per piece
  float spread_gap;
  ControlValue *values; // arr: current control values
  const char **missing; // arr: wire ends that name nothing
  float flow_speed;
  float explode;
  // Making room around a selection.
  Vec3 *room_to;      // arr per piece
  bool room_moving;
  Arena ar;
  bool wires_dirty;
} Model;

typedef struct {
  bool paint; // make painted textures (off in tests)
} BuildOpts;

Model *model_build(AssemblySpec *spec, BuildOpts opts);
void model_free(Model *m);

// Recompute every node's world matrix, and re-lay wires whose ends moved.
void model_update(Model *m);
// Advance spins, flows and pulses by dt seconds at the model's current flow speed and values.
void model_tick(Model *m, float dt);
void model_reset_anim(Model *m);
void model_set_value(Model *m, const char *id, double v);
double model_get_value(const Model *m, const char *id);
// Carry values across dives, keeping those already set.
void model_start_values(Model *m, const ControlValue *keep, int n);

void model_set_explode(Model *m, float amount);
void model_make_room(Model *m, int node); // -1 clears
bool model_room_tick(Model *m, float dt); // true while moving

Box node_box_now(const Model *m, int node);
Box node_copy_box_now(const Model *m, int node, int copy);
int node_top_piece(const Model *m, int node); // its entry in pieces, or -1
int model_find(const Model *m, const char *name);
int model_first_detail(const Model *m); // first part with a deeper model, or -1

// What a click or hover lands on.
typedef struct {
  int node; // -1: nothing
  int copy; // -1: the whole part
  float t;
  Vec3 point;
} Hit;
Hit model_pick(const Model *m, Vec3 origin, Vec3 dir);

// Named surface presets: the spec's `material` field.
typedef struct {
  const char *name, *color;
  float metalness, roughness, opacity, clearcoat;
  bool transparent;
} MaterialPreset;
extern const MaterialPreset MATERIAL_PRESETS[];
extern const int MATERIAL_PRESET_COUNT;

extern const float SPIN_SLOWDOWN;

// Wire kinds: insulation colour, what flows, shader mode, the word the inspector uses.
typedef struct {
  const char *name, *color, *glow, *label;
  int mode;
} WireKind;
extern const WireKind WIRE_KINDS[7];
const WireKind *wire_kind(const char *name);

// Hand a texture key to the cache; NULL when it has to be painted.
Texture *texture_cached(const char *key);
Texture *texture_put(const char *key, int w, int h, u8 *rgba, bool repeat);
void texture_cache_each(void (*fn)(Texture *, void *), void *ud);
// Drop every texture not marked since the last clear; `release` frees its GPU side first.
void texture_cache_clear_marks(void);
void texture_cache_sweep(void (*release)(Texture *));
// One part of a bus's footprints list; exposed for the painted silkscreen.
typedef struct {
  const char *name;
  float x, z, w, d;
  bool silk;
} Footprint;
Footprint *layer_footprints(const LayerSpec *l); // arr
Mat4 copy_matrix(const PartSpec *p, int i, int count, float x, float y, float z, const Footprint *avoid, int navoid);
int part_copies(const PartSpec *p);
