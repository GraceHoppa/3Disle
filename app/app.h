// The parts of the command that talk to each other.
#pragma once
#include "base.h"

int app_window(int argc, char **argv);             // window.c: the desktop app
int cmd_script(int argc, char **argv);             // script.c: headless driving, for checks
bool write_png(const char *path, int w, int h, const u8 *rgba);
