// The GPU side: OpenGL ES 3 only, so the same code runs natively and as WebGL2. Forward physically
// based shading into HDR, then bloom, grade, ACES and grain in one final pass.
#pragma once
#include "model.h"

typedef struct {
  Vec3 pos, target;
  float fov_deg, near_z, far_z;
} Camera;

typedef struct {
  float bloom;      // intensity, default 1.1
  float threshold;  // default 0.62
  float grain;      // opacity, default 0.08
  float vignette;   // darkness, default 0.62
  float contrast, brightness, saturation, hue_deg;
  float chromatic;  // offset, default 0.0006
  float fade;       // 0 clear, 1 black
  float time;       // seconds; seeds the grain
} PostParams;

PostParams post_defaults(void);

bool r_init(char *err, size_t errlen);
void r_shutdown(void);

// Output size in pixels. `scale` renders the 3D above that (2 for offline video), `msaa` 0 or 4.
void r_set_size(int w, int h, float scale, int msaa);

typedef struct {
  int hover_node, hover_copy;     // hovered part glows faintly
} SceneHints;

// Draw a model into the HDR buffer; `dest_fbo` then receives the graded image at the output size.
void r_render(const Model *m, const Camera *cam, const PostParams *post, const SceneHints *hints, u32 dest_fbo);
// Nothing loaded: background only.
void r_render_empty(const PostParams *post, u32 dest_fbo);

Mat4 camera_view_proj(const Camera *c, float aspect);
// Screen-to-world ray: x, y in pixels with y down.
void camera_ray(const Camera *c, float aspect, float x, float y, float w, float h, Vec3 *origin, Vec3 *dir);
// World to pixels; false when behind the camera.
bool camera_project(const Camera *c, float aspect, Vec3 p, float w, float h, float *sx, float *sy);

// Free GPU copies of a model's meshes before freeing the model.
void r_release_model(Model *m);
void r_release_texture(Texture *t);

// An offscreen target to read frames back from.
u32 r_offscreen_fbo(int w, int h);
void r_free_offscreen(u32 fbo);
void r_read_rgba(u32 fbo, int w, int h, u8 *out); // top row first
