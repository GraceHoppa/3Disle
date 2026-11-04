// Offline video: every frame drawn on purpose, never against a clock, then piped to ffmpeg.
#pragma once
#include "render.h"
#include "shot.h"
#include "ui.h"

typedef struct {
  const char *out;
  int w, h, fps, crf;
  float scale; // 3D supersampling
  double from, to;
  bool grain;
} VideoOpts;

typedef struct Video Video;

// Draw the labels and captions of a moment over the frame.
void shot_draw_overlay(const Shot *s, const ShotFrame *f, double t, float w, float h, float aspect);

Video *video_begin(Shot *shot, ShotPlayer *p, const VideoOpts *o, char *err, size_t errlen);
// Draws and sends one frame. False when finished or failed; see video_error.
bool video_frame(Video *v);
float video_progress(const Video *v);
int video_frame_index(const Video *v);
int video_frame_count(const Video *v);
u32 video_fbo(const Video *v);
const char *video_error(const Video *v);
bool video_end(Video *v); // true when the file is complete

// One frame as pixels at time t, for previews and determinism checks. Caller frees.
u8 *video_still(Shot *shot, ShotPlayer *p, const VideoOpts *o, double t);
