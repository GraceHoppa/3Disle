// Triangle meshes and the shape builders. Everything is in millimetres, y up.
#pragma once
#include "base.h"

typedef struct {
  float px, py, pz, nx, ny, nz, u, v, along, lane;
} Vertex;

typedef struct {
  int start, count; // in indices when indexed, else in vertices
  int mat;
} MeshGroup;

typedef struct {
  Vertex *v;  // arr
  u32 *idx;   // arr; NULL for a plain triangle list
  MeshGroup groups[6];
  int ngroups;
  Box box;
  u32 gl_vao, gl_vbo, gl_ibo;
  int gl_vcount, gl_icount;
  bool gl_dirty;
} Mesh;

// A flat outline with holes, as closed loops without a repeated last point.
typedef struct {
  Vec2 *outer;  // arr
  Vec2 **holes; // arr of arr
} Outline;

Mesh *mesh_new(void);
void mesh_free(Mesh *m);
void mesh_update_box(Mesh *m);
void mesh_transform(Mesh *m, Mat4 t);    // positions and normals
void mesh_to_triangles(Mesh *m);         // drop the index
void mesh_flat_normals(Mesh *m);         // one normal per triangle (needs a triangle list)
void mesh_append(Mesh *dst, const Mesh *src); // both triangle lists
void mesh_mirror_x(Mesh *m);             // keeps faces facing out
int mesh_tri_count(const Mesh *m);

Mesh *mesh_box(float w, float h, float d);                        // 6 groups: +x -x +y -y +z -z
Mesh *mesh_rounded_box(float w, float h, float d, int segments, float radius);
Mesh *mesh_cylinder(float radius, float height, int radial);
Mesh *mesh_sphere(float radius, int wseg, int hseg);
Mesh *mesh_plane_y(float w, float d, float y);                    // facing +y, canvas top at -z
Mesh *mesh_lathe(const Vec2 *pts, int n, int segments);
// Extrude along +z in the outline's own plane; bevel 0 for none.
Mesh *mesh_extrude(const Outline *o, float depth, float bevel_thick, float bevel_size, int bevel_segs);

Mesh *mesh_fan(float radius, float height, int blades, bool ring, bool prop);
Mesh *mesh_holed_slab(float w, float d, float t, const Vec2 *centers, const float *radii, int n);
Mesh *mesh_outline(const Vec2 *pts, int npts, float t, const Vec2 *centers, const float *radii, int n);
Mesh *mesh_port(const char *kind, float w, float h, float d);

// A round wire along a smooth curve through the points. Vertices carry `along` 0..1.
float curve_length(const Vec3 *pts, int n);
void mesh_tube(Mesh *m, const Vec3 *pts, int npts, int segments, float radius);

void outline_free(Outline *o);
// Triangulate outer + holes: indices into outer followed by each hole. arr of u32.
u32 *earcut(const Outline *o);
// 45-degree routing: orthogonal, diagonal, orthogonal. Writes four points.
void route45(float ax, float ay, float bx, float by, float bias, Vec2 out[4]);
