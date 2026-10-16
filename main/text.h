#pragma once
// Text the game draws itself (the HUD, the desk, the Stories screen's
// lines): at the size it asks for, or, with Large text on (settings.h),
// larger, in strokes two pixels wide, over a dark shadow. The engine's Hershey font draws
// one-pixel lines: at 12 px its capitals are 7 px high, hard to read on
// the badge's screen. Normal draws exactly as rendertext_draw does.

#include <stdbool.h>
#include <stdint.h>
#include "pax_gfx.h"

void      text_set_large(bool on);
bool      text_large(void);
// The height `size` is drawn at.
float     text_px(float size);
pax_vec2f text_size(float size, char const* s);
pax_vec2f text_draw(pax_buf_t* fb, uint32_t col, float size, float x, float y, char const* s);
// At `size` always, but with Large text in two-pixel strokes over a shadow:
// for a panel laid out to the pixel (the debugger's).
pax_vec2f text_draw_firm(pax_buf_t* fb, uint32_t col, float size, float x, float y, char const* s);
