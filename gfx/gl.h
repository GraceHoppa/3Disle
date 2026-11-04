// One GL header for every build: ES 3 natively, the same calls forwarded to WebGL2 on the web.
#pragma once
#include <GLES3/gl3.h>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

// Turn on what rendering into half-float targets needs (WebGL2 asks for it by name).
void gl_extensions(void);
