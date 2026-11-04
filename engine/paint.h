// A small software canvas, for the painted finishes: PCB traces, carbon weave, silicon, chip
// markings. Pure CPU, 8-bit sRGB, the same order of strokes every time for the same seed.
#pragma once
#include "base.h"
#include "model.h"

void surface_size(float width, float depth, float pxmm, int *w, int *h);
// All return malloc'd RGBA, w*h*4 bytes, first row is the canvas top.
u8 *paint_surface(const char *kind, int w, int h, float pxmm, float width, float depth, const Footprint *prints, int nprints, u32 seed, const char *base);
u8 *paint_pattern(const char *kind, int w, int h, const char *base, int seed, int cx, int cy);
u8 *paint_marking(const char *text, int w, int h, bool exact);

// The fonts, shared with the interface.
const u8 *font_regular(void);
const u8 *font_bold(void);
