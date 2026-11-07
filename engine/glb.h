// Model files: a parsed GLB or STL, cached by path. Geometry is baked into the file's own space, so a
// part can take any set of its objects and they stay where the author of the model put them.
#pragma once
#include "mesh.h"

typedef struct {
  const char *name; // as written in the file
  int parent;       // -1 at the top
  int *prims;       // arr: its own geometry
  bool has_geometry; // here or anywhere below
} ModelObject;

typedef struct {
  Vec3 *pos, *nor; // arr
  Vec2 *uv;        // arr, or NULL
  u32 *idx;        // arr, in triangles; never NULL
  int material;    // -1: none
  Box box;
} ModelPrim;

typedef struct {
  const char *name;
  Vec3 color;   // linear
  float alpha, metalness, roughness;
  Vec3 emissive;
  int image, mr_image; // into images, or -1
  bool blend, two_sided;
} ModelMaterial;

typedef struct {
  const u8 *data; // inside the file's bytes
  int len;
} ModelImage;

typedef struct {
  Arena ar;
  u8 *bytes;
  size_t nbytes;
  char *path;
  bool is_stl;
  ModelObject *objects; // arr; parents before children
  ModelPrim *prims;     // arr
  ModelMaterial *materials; // arr
  ModelImage *images;   // arr
  Box box;
  double units;         // file units to millimetres: 1000 for glTF (metres), 1 for STL
  const char *credit;   // title, author, licence as the file states them; NULL if it says nothing
} ModelFile;

// `bytes` is taken over. NULL with `err` filled when the file can't be used; nothing in it can make
// this read outside the buffer.
ModelFile *modelfile_parse(u8 *bytes, size_t n, const char *path, char *err, size_t errlen);
void modelfile_free(ModelFile *f);
// Cached: the same path gives the same file until the program ends or the cache is cleared.
ModelFile *modelfile_load(const char *path, char *err, size_t errlen);
void modelfile_cache_clear(void);

// Which objects `names` picks, with everything below them. NULL or empty: all of them. arr of bool,
// one per object. A name matches as written, or the way three.js rewrites it (spaces to `_`, no `[].:/`).
bool *modelfile_pick(const ModelFile *f, const char *const *names, int nnames);
// Decoded RGBA, for the caller to free; NULL if it can't be decoded.
u8 *modelfile_image(const ModelFile *f, int image, int *w, int *h);
// The pieces worth calling parts: going down from the top while there is only one way down.
int *modelfile_natural_parts(const ModelFile *f); // arr of object indices
