// A device as data: the typed form of an assembly JSON file. The editor changes this and saves it
// back; the builder (model.c) turns it into something to draw. Fields left out of the file stay
// unset (NAN, NULL, has_x false), so saving writes back only what the author wrote.
#pragma once
#include "json.h"

#define UNSET ((double)NAN)
static inline bool is_set(double v) { return !isnan(v); }

typedef struct {
  double x, y;
} Dbl2;
typedef struct {
  double x, y, z;
} Dbl3;

// A number, or a rule that follows a control: { "by": "throttle", "map": [[0, 0], [100, 29000]] }.
typedef struct {
  bool set, is_rule;
  double num;
  const char *by;
  Dbl2 *map; // arr
} Value;

typedef struct {
  const char *key, *val;
} KV;

typedef struct {
  const char *id, *label, *unit, *info;
  double min, max, step, value;
  Json *extra;
} ControlSpec;

typedef struct {
  const char *label, *unit, *by;
  Dbl2 *map; // arr
  double digits;
  Json *extra;
} ReadoutSpec;

typedef struct {
  Dbl2 at;
  double radius;
} Hole;

typedef enum { SHAPE_BOX, SHAPE_CYLINDER, SHAPE_SPHERE, SHAPE_FAN, SHAPE_PORT, SHAPE_OUTLINE, SHAPE_FILE, SHAPE_COUNT } Shape;
extern const char *const SHAPE_NAMES[SHAPE_COUNT];

typedef struct {
  bool on;
  bool has_step, has_grid, has_around, has_start;
  double count;
  Dbl2 step, grid;
  double around, start_angle;
  const char **names; // arr
} Repeat;

typedef struct {
  const char *name, *info, *category, *detail, *color, *material, *file, *pattern, *marking;
  const char *finish, *port, *blade;
  const char *glow; // NULL: none; "" : accent green; else a colour
  Shape shape;
  bool has_at, has_size, has_scatter, has_cells;
  Dbl2 at, scatter, cells;
  Dbl3 size;
  double radius, height, round, lift, facing, blades;
  bool has_ring, ring, has_mirror, mirror;
  Value spin;
  bool has_pulse;
  double pulse_phase;
  Value pulse_level;
  const char *pulse_color;
  Dbl2 *points;  // arr
  Hole *cutouts; // arr
  bool has_cutouts, has_points;
  Repeat repeat;
  KV *specs; // arr
  bool has_specs;
  Json *extra;
} PartSpec;

typedef struct {
  const char *name, *from, *color, *kind, *info, *category;
  const char **to; // arr
  bool to_is_array;
  double lanes, width;
  Value level;
  KV *specs;
  bool has_specs;
  Json *extra;
} LinkSpec;

typedef struct {
  const char *name, *info, *category, *detail, *color, *material, *surface, *glow;
  double thickness, base;
  PartSpec *parts; // arr
  bool has_parts;
  LinkSpec *links; // arr
  bool has_links;
  Hole *holes; // arr
  bool has_holes;
  KV *specs;
  bool has_specs;
  Json *extra;
} LayerSpec;

typedef struct {
  const char *name, *from, *to, *kind, *color, *info, *category;
  double phase, gauge;
  Value level;
  Dbl3 *via; // arr
  bool has_via;
  KV *specs;
  bool has_specs;
  Json *extra;
} WireSpec;

typedef struct {
  Arena ar;
  const char *name;
  Dbl2 footprint;
  LayerSpec *layers;     // arr
  WireSpec *wires;       // arr
  ControlSpec *controls; // arr
  ReadoutSpec *readouts; // arr
  bool has_wires, has_controls, has_readouts;
  Json *extra;
} AssemblySpec;

// Parsing never fails on missing fields; only broken JSON or a file that is no assembly does.
AssemblySpec *spec_parse(const char *text, char *err, size_t errlen);
AssemblySpec *spec_load(const char *path, char *err, size_t errlen);
AssemblySpec *spec_new(const char *name, double w, double d);
void spec_free(AssemblySpec *s);
Json *spec_to_json(Arena *ar, const AssemblySpec *s);
char *spec_write(const AssemblySpec *s); // free() the result
bool spec_save(const AssemblySpec *s, const char *path);
AssemblySpec *spec_clone(const AssemblySpec *s);

// Editor helpers: the new item starts with sensible values and a free name.
PartSpec *spec_add_part(AssemblySpec *s, int layer, Shape shape);
int spec_add_layer(AssemblySpec *s, int at, const char *name);
WireSpec *spec_add_wire(AssemblySpec *s, const char *from, const char *to);
void spec_part_defaults(AssemblySpec *s, PartSpec *p, Shape shape);
const char *spec_str(AssemblySpec *s, const char *text); // copy into the document

// Values and rules.
double lookup(const Dbl2 *map, double x);
typedef struct {
  const char *id;
  double value;
} ControlValue;
double value_eval(const Value *v, const ControlValue *values, int n, double fallback);
// "21 400 rpm": thin-spaced thousands, the unit after a space.
void format_readout(char *out, size_t len, double value, const char *unit, int digits);
