// The 3disle command. Subcommands grow here; `shot` draws one frame of a device to a PNG.
#include "gl.h"
#include "headless.h"
#include "render.h"
#include "spec.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void gl_extensions(void) {}

static Camera frame_three_quarter(const Model *m, float aspect) {
  Camera c = {.fov_deg = 40};
  float hfov = 2 * atanf(tanf(c.fov_deg * PI / 360) * aspect);
  float vfov = c.fov_deg * PI / 180;
  float dist = m->radius / sinf(minf(vfov, hfov) / 2) * 1.15f;
  Vec3 dir = v3norm(v3(1, 0.7f, 1.2f));
  c.target = m->center;
  c.pos = v3mad(m->center, dir, dist);
  c.near_z = m->radius / 100;
  c.far_z = m->radius * 100;
  return c;
}

static int cmd_shot(int argc, char **argv) {
  if (argc < 2) return fprintf(stderr, "usage: 3disle shot <model.json> <out.png> [width height explode throttle]\n"), 2;
  const char *in = argv[0], *out = argv[1];
  int w = argc > 2 ? atoi(argv[2]) : 1400, h = argc > 3 ? atoi(argv[3]) : 850;
  float explode = argc > 4 ? (float)atof(argv[4]) : 0;
  char err[2048] = "";
  AssemblySpec *s = spec_load(in, err, sizeof err);
  if (!s) return fprintf(stderr, "%s\n", err), 1;
  if (!headless_init(err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  printf("GL: %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  if (!r_init(err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  Model *m = model_build(s, (BuildOpts){.paint = true});
  model_set_explode(m, explode);
  model_tick(m, 0.4f);
  model_update(m);
  r_set_size(w, h, 2, 4);
  u32 fbo = r_offscreen_fbo(w, h);
  Camera cam = frame_three_quarter(m, (float)w / (float)h);
  PostParams post = post_defaults();
  r_render(m, &cam, &post, NULL, fbo);
  u8 *px = malloc((size_t)w * (size_t)h * 4);
  r_read_rgba(fbo, w, h, px);
  GLenum e = glGetError();
  if (e) fprintf(stderr, "GL error 0x%x\n", e);
  int ok = stbi_write_png(out, w, h, 4, px, w * 4);
  free(px);
  headless_shutdown();
  return ok ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "shot")) return cmd_shot(argc - 2, argv + 2);
  fprintf(stderr, "usage: 3disle shot <model.json> <out.png>\n");
  return 2;
}
