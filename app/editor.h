// The app: Build a device, Animate a video of it, View it the way a visitor will. One window.
#pragma once
#include "ui.h"

typedef struct Editor Editor;

Editor *editor_new(const char *data_root);
// A device, tour or video (.shot.json). NULL path: a new, empty device.
bool editor_open(Editor *e, const char *path, char *err, size_t errlen);
void editor_frame(Editor *e, const UiInput *in, u32 fbo);
// The window was asked to close. True: nothing is at stake, go. False: the editor is asking.
bool editor_request_close(Editor *e);
bool editor_should_close(const Editor *e);
void editor_free(Editor *e);
// For checks: what the app holds right now, as text. False when `what` means nothing.
bool editor_query(Editor *e, const char *what, char *out, size_t n);
