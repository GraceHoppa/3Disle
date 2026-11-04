// A GL context with no window: renders on the GPU through EGL, for video and for tests.
#pragma once
#include <stdbool.h>
#include <stddef.h>

bool headless_init(char *err, size_t errlen);
void headless_shutdown(void);
