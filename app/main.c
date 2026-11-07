// The 3disle command: the app when run plainly, and everything headless behind subcommands.
#include "app.h"
#include "gl.h"
#include "headless.h"
#include "render.h"
#include "spec.h"
#include "ui.h"
#include "video.h"
#include "viewer.h"
#include <unistd.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void gl_extensions(void) {}

static void usage(void) {
  fprintf(stderr,
          "3disle [file.json]                 the app: build, animate, view (a device, tour or video)\n"
          "3disle view [file.json]            just the viewer, on a device or tour\n"
          "3disle shot <file.json> <out.png> [--size WxH] [--explode A]\n"
          "3disle auto <device-or-tour.json> <out.shot.json>   write a video's timeline from the data\n"
          "3disle render <file.shot.json> -o out.mp4 [--size WxH] [--fps N] [--crf N] [--ss K] [--from S] [--to S] [--no-grain]\n"
          "3disle still <file.shot.json> <out.png> <seconds> [--size WxH]\n"
          "3disle export <device-or-tour.json> -o page.html    one file that opens from disk\n"
          "3disle check [folder-or-file]      every file opens, and everything it names exists\n"
          "3disle script [--app] <file.json> <steps>  drive the viewer or the app headless (for checks)\n");
}

static bool parse_size(const char *s, int *w, int *h) { return sscanf(s, "%dx%d", w, h) == 2 && *w > 0 && *h > 0; }

bool write_png(const char *path, int w, int h, const u8 *rgba) { return stbi_write_png(path, w, h, 4, rgba, w * 4) != 0; }

static int cmd_shot(int argc, char **argv) {
  if (argc < 2) return usage(), 2;
  const char *in = argv[0], *out = argv[1];
  int w = 1400, h = 850;
  float explode = 0;
  for (int i = 2; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--size") && !parse_size(argv[i + 1], &w, &h)) return usage(), 2;
    if (!strcmp(argv[i], "--explode")) explode = (float)atof(argv[i + 1]);
  }
  char err[2048] = "";
  AssemblySpec *s = spec_load(in, err, sizeof err);
  if (!s) return fprintf(stderr, "%s\n", err), 1;
  if (!headless_init(err, sizeof err) || !r_init(err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  Model *m = model_build(s, (BuildOpts){.paint = true});
  model_set_explode(m, explode);
  model_tick(m, 0.4f);
  model_update(m);
  r_set_size(w, h, 2, 4);
  u32 fbo = r_offscreen_fbo(w, h);
  Stage st;
  stage_init(&st);
  stage_frame(&st, m, (float)w / (float)h, 1);
  PostParams post = post_defaults();
  r_render(m, &st.cam, &post, NULL, fbo);
  u8 *px = malloc((size_t)w * (size_t)h * 4);
  r_read_rgba(fbo, w, h, px);
  bool ok = write_png(out, w, h, px);
  free(px);
  headless_shutdown();
  return ok ? 0 : 1;
}

static int cmd_auto(int argc, char **argv) {
  if (argc < 2) return usage(), 2;
  char err[1024] = "";
  Shot *s = shot_auto(argv[0], argv[1], err, sizeof err);
  if (!s) return fprintf(stderr, "%s\n", err), 1;
  if (!shot_save(s, argv[1])) return fprintf(stderr, "can't write %s\n", argv[1]), 1;
  printf("%s: %d scenes, %.1f s\n", argv[1], arr_len(s->scenes), s->duration);
  shot_free(s);
  return 0;
}

static int cmd_export(int argc, char **argv) {
  const char *in = NULL, *out = NULL;
  for (int i = 0; i < argc; i++) {
    if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
    else in = argv[i];
  }
  if (!in || !out) return usage(), 2;
  char err[2048] = "";
  if (!export_page(in, out, err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  return 0;
}

static bool video_args(VideoOpts *o, const Shot *s, int argc, char **argv, int first) {
  o->w = s->w, o->h = s->h, o->fps = s->fps, o->crf = 18, o->scale = 2, o->grain = true;
  for (int i = first; i < argc; i++) {
    bool more = i + 1 < argc;
    if (!strcmp(argv[i], "-o") && more) o->out = argv[++i];
    else if (!strcmp(argv[i], "--size") && more) {
      if (!parse_size(argv[++i], &o->w, &o->h)) return false;
    } else if (!strcmp(argv[i], "--fps") && more) o->fps = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--crf") && more) o->crf = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--ss") && more) o->scale = (float)atof(argv[++i]);
    else if (!strcmp(argv[i], "--from") && more) o->from = atof(argv[++i]);
    else if (!strcmp(argv[i], "--to") && more) o->to = atof(argv[++i]);
    else if (!strcmp(argv[i], "--no-grain")) o->grain = false;
    else return false;
  }
  o->w &= ~1, o->h &= ~1;
  return o->fps > 0 && o->w > 0 && o->h > 0;
}

static int cmd_render(int argc, char **argv) {
  if (argc < 1) return usage(), 2;
  char err[2048] = "";
  Shot *s = shot_load(argv[0], err, sizeof err);
  if (!s) return fprintf(stderr, "%s\n", err), 1;
  VideoOpts o = {0};
  if (!video_args(&o, s, argc, argv, 1) || !o.out) return usage(), 2;
  if (!headless_init(err, sizeof err) || !r_init(err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  ui_init();
  ShotPlayer *p = player_new(s, (BuildOpts){.paint = true}, err, sizeof err);
  if (!p) return fprintf(stderr, "%s\n", err), 1;
  Video *v = video_begin(s, p, &o, err, sizeof err);
  if (!v) return fprintf(stderr, "%s\n", err), 1;
  int total = video_frame_count(v), shown = -1;
  while (video_frame(v)) {
    int pct = video_frame_index(v) * 100 / total;
    if (pct != shown) {
      printf("\r%3d%%  frame %d of %d", pct, video_frame_index(v), total);
      fflush(stdout);
      shown = pct;
    }
  }
  const char *verr = video_error(v);
  if (verr) fprintf(stderr, "\n%s\n", verr);
  bool ok = video_end(v);
  printf("\n%s %s\n", ok ? "wrote" : "failed:", o.out);
  player_free(p);
  shot_free(s);
  headless_shutdown();
  return ok ? 0 : 1;
}

static int cmd_still(int argc, char **argv) {
  if (argc < 3) return usage(), 2;
  char err[2048] = "";
  Shot *s = shot_load(argv[0], err, sizeof err);
  if (!s) return fprintf(stderr, "%s\n", err), 1;
  VideoOpts o = {0};
  if (!video_args(&o, s, argc, argv, 3)) return usage(), 2;
  if (!headless_init(err, sizeof err) || !r_init(err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
  ui_init();
  ShotPlayer *p = player_new(s, (BuildOpts){.paint = true}, err, sizeof err);
  if (!p) return fprintf(stderr, "%s\n", err), 1;
  u8 *px = video_still(s, p, &o, atof(argv[2]));
  bool ok = write_png(argv[1], o.w, o.h, px);
  free(px);
  player_free(p);
  shot_free(s);
  headless_shutdown();
  return ok ? 0 : 1;
}

int main(int argc, char **argv) {
  const char *cmd = argc > 1 ? argv[1] : "";
  if (!strcmp(cmd, "shot")) return cmd_shot(argc - 2, argv + 2);
  if (!strcmp(cmd, "auto")) return cmd_auto(argc - 2, argv + 2);
  if (!strcmp(cmd, "render")) return cmd_render(argc - 2, argv + 2);
  if (!strcmp(cmd, "still")) return cmd_still(argc - 2, argv + 2);
  if (!strcmp(cmd, "export")) return cmd_export(argc - 2, argv + 2);
  if (!strcmp(cmd, "check")) return cmd_check(argc - 2, argv + 2);
  if (!strcmp(cmd, "script")) return cmd_script(argc - 2, argv + 2);
  if (!strcmp(cmd, "--help") || !strcmp(cmd, "-h")) return usage(), 0;
  // Only a real file or `view` may open a window: a mistyped command must never pop one up.
  if (argc > 1 && strcmp(cmd, "view") && access(cmd, R_OK)) {
    fprintf(stderr, "3disle: no command or file called %s\n", cmd);
    usage();
    return 2;
  }
  return app_window(argc - 1, argv + 1);
}
