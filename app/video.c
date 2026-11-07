#include "video.h"
#include "gl.h"
#include "viewer.h"
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

struct Video {
  Shot *shot;
  ShotPlayer *player;
  VideoOpts o;
  u32 fbo;
  int frame0, frames, at;
  u8 *px;
  int pipe_fd;
  pid_t pid;
  char err[256];
  bool failed;
};

static PostParams post_for(const VideoOpts *o, const ShotFrame *f, double t) {
  PostParams p = post_defaults();
  p.fade = f->fade;
  p.time = (float)fmod(t, 100);
  if (!o->grain) p.grain = 0;
  return p;
}

static float fade_win(double t, double t0, double t1, double dur) {
  float in = clampf((float)((t - t0) / dur), 0, 1), out = clampf((float)((t1 - t) / dur), 0, 1);
  return minf(in, out);
}

void shot_draw_overlay(const Shot *s, const ShotFrame *f, double t, float w, float h, float aspect) {
  if (!f->model) return;
  Camera cam = {f->cam_pos, f->cam_target, f->fov, f->near_z, f->far_z};
  // Labels, kept from covering each other: earlier ones keep their place.
  struct {
    float x, y, w;
  } placed[32];
  int np = 0;
  for (int i = 0; i < arr_len(s->labels); i++) {
    const ShotLabel *l = &s->labels[i];
    if (l->scene != f->scene || t < l->t0 || t > l->t1 || np >= 32) continue;
    int id = model_find(f->model, l->part);
    if (id < 0) continue;
    Box b = node_box_now(f->model, id);
    Vec3 c = box_center(b);
    float x, y;
    if (!camera_project(&cam, aspect, v3(c.x, b.max.y, c.z), w, h, &x, &y)) continue;
    if (x <= 0 || x >= w || y <= 0 || y >= h) continue;
    const char *text = l->text ? l->text : l->part;
    float tw = ui_text_width(S(11), FONT_REGULAR, S(0.66f), text) + S(12);
    float ty = y - S(14);
    bool clear = false;
    for (int k = 0; k < 4; k++) {
      int hit = -1;
      for (int q = 0; q < np && hit < 0; q++)
        if (fabsf(placed[q].x - x) < (placed[q].w + tw) / 2 + S(8) && fabsf(placed[q].y - ty) < S(22)) hit = q;
      if (hit < 0) {
        clear = true;
        break;
      }
      ty = placed[hit].y - S(23);
    }
    if (!clear) continue;
    placed[np].x = x, placed[np].y = ty, placed[np].w = tw;
    np++;
    draw_tag(x, ty, y - ty, text, fade_win(t, l->t0, l->t1, 0.3));
  }
  if (f->model->credit) {
    float cw = minf(ui_text_width(S(11), FONT_REGULAR, 0, f->model->credit), w * 0.6f);
    ui_text_fit(w - cw - S(16), h - S(26), cw + 1, S(11), FONT_REGULAR, c_alpha(C_DIM, 0.8f), f->model->credit);
  }
  for (int i = 0; i < arr_len(s->captions); i++) {
    const ShotCaption *c = &s->captions[i];
    if (t < c->t0 || t > c->t1) continue;
    float a = clampf((float)((c->t1 - t) / 0.45), 0, 1);
    float rt = (float)(t - c->t0);
    draw_hud(S(48), h - S(56), minf(S(620), w - S(96)), c->scale, c->title, c->text, c->caption, rt, rt, a);
  }
}

static void draw_frame(Shot *s, ShotPlayer *p, const VideoOpts *o, double t, u32 fbo) {
  float aspect = (float)o->w / (float)o->h;
  ShotFrame f = player_seek(p, t, aspect);
  PostParams post = post_for(o, &f, t);
  if (f.model) {
    Camera cam = {f.cam_pos, f.cam_target, f.fov, f.near_z, f.far_z};
    r_render(f.model, &cam, &post, NULL, fbo);
  } else {
    r_render_empty(&post, fbo);
  }
  UiInput in = {0};
  in.w = o->w, in.h = o->h;
  // Interface sizes follow the video's height, so 1080p and 4K frame alike.
  in.scale = (float)o->h / 900;
  in.mx = -1, in.my = -1;
  ui_begin(&in);
  shot_draw_overlay(s, &f, t, (float)o->w, (float)o->h, aspect);
  ui_render(fbo);
}

u8 *video_still(Shot *shot, ShotPlayer *p, const VideoOpts *o, double t) {
  r_set_size(o->w, o->h, o->scale, 4);
  static u32 fbo;
  static int fw, fh;
  if (!fbo || fw != o->w || fh != o->h) {
    fbo = r_offscreen_fbo(o->w, o->h);
    fw = o->w, fh = o->h;
  }
  draw_frame(shot, p, o, t, fbo);
  u8 *px = malloc((size_t)o->w * (size_t)o->h * 4);
  r_read_rgba(fbo, o->w, o->h, px);
  return px;
}

Video *video_begin(Shot *shot, ShotPlayer *p, const VideoOpts *o, char *err, size_t errlen) {
  Video *v = calloc(1, sizeof *v);
  v->shot = shot;
  v->player = p;
  v->o = *o;
  v->o.w &= ~1, v->o.h &= ~1; // even, as yuv420p needs
  double end = o->to > 0 ? fmin(o->to, shot->duration) : shot->duration;
  v->frame0 = (int)ceil(o->from * o->fps - 1e-6);
  v->frames = maxi((int)ceil(end * o->fps - 1e-6) - v->frame0, 0);
  if (!v->frames) {
    if (err) snprintf(err, errlen, "nothing to render: the video has no length");
    free(v);
    return NULL;
  }
  r_set_size(v->o.w, v->o.h, o->scale, 4);
  v->fbo = r_offscreen_fbo(v->o.w, v->o.h);
  v->px = malloc((size_t)v->o.w * (size_t)v->o.h * 4);

  int fd[2];
  if (pipe(fd)) {
    if (err) snprintf(err, errlen, "pipe: %s", strerror(errno));
    free(v->px);
    free(v);
    return NULL;
  }
  signal(SIGPIPE, SIG_IGN);
  v->pid = fork();
  if (v->pid == 0) {
    dup2(fd[0], 0);
    close(fd[0]);
    close(fd[1]);
    char size[32], rate[16], crf[16];
    snprintf(size, sizeof size, "%dx%d", v->o.w, v->o.h);
    snprintf(rate, sizeof rate, "%d", o->fps);
    snprintf(crf, sizeof crf, "%d", o->crf);
    execlp("ffmpeg", "ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", size, "-r", rate, "-i", "-", "-c:v", "libx264",
           "-preset", "slow", "-crf", crf, "-pix_fmt", "yuv420p", "-movflags", "+faststart", o->out, (char *)NULL);
    _exit(127);
  }
  close(fd[0]);
  if (v->pid < 0) {
    if (err) snprintf(err, errlen, "fork: %s", strerror(errno));
    close(fd[1]);
    free(v->px);
    free(v);
    return NULL;
  }
  v->pipe_fd = fd[1];
  return v;
}

bool video_frame(Video *v) {
  if (v->failed || v->at >= v->frames) return false;
  double t = (double)(v->frame0 + v->at) / v->o.fps;
  draw_frame(v->shot, v->player, &v->o, t, v->fbo);
  r_read_rgba(v->fbo, v->o.w, v->o.h, v->px);
  size_t left = (size_t)v->o.w * (size_t)v->o.h * 4;
  const u8 *d = v->px;
  while (left) {
    ssize_t n = write(v->pipe_fd, d, left);
    if (n < 0) {
      if (errno == EINTR) continue;
      snprintf(v->err, sizeof v->err, "ffmpeg stopped taking frames (is it installed?)");
      v->failed = true;
      return false;
    }
    d += n;
    left -= (size_t)n;
  }
  v->at++;
  return v->at < v->frames;
}

float video_progress(const Video *v) { return (float)v->at / (float)maxi(v->frames, 1); }
int video_frame_index(const Video *v) { return v->at; }
int video_frame_count(const Video *v) { return v->frames; }
u32 video_fbo(const Video *v) { return v->fbo; }
const char *video_error(const Video *v) { return v->err[0] ? v->err : NULL; }

bool video_end(Video *v) {
  close(v->pipe_fd);
  int status = 0;
  waitpid(v->pid, &status, 0);
  bool ok = !v->failed && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  r_free_offscreen(v->fbo);
  free(v->px);
  free(v);
  return ok;
}
