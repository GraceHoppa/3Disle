// The parts of the command that talk to each other.
#pragma once
#include "base.h"

int app_window(int argc, char **argv);             // window.c: the desktop app
int cmd_script(int argc, char **argv);             // script.c: headless driving, for checks
int cmd_check(int argc, char **argv);              // check.c: everything in the data resolves
bool write_png(const char *path, int w, int h, const u8 *rgba);
bool export_page(const char *entry, const char *out_path, char *err, size_t errlen); // export.c
