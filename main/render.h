#pragma once
// Drawing a chamber, and the views through its portals.
//
// SynthEngine3D has no stencil buffer, and does not need one here. Each
// view through a portal is drawn first, deepest first, every triangle
// clipped in world space to the planes through the eye and the portal's
// edges -- so it paints exactly the portal's opening and nothing else.
// scene_begin() then starts the next pass with a fresh depth buffer and
// the pixels left as they are. The room itself goes last, with the
// portals' faces cut out of its mesh: it paints everything except the
// openings, so the views show through them.

#include "game.h"
#include "level.h"
#include "pax_gfx.h"
#include "player.h"
#include "portal.h"

#define RENDER_PORTAL_DEPTH_MAX 3

void render_init(char const* texture_dir);

// Re-mesh the chamber; on loading it and whenever a portal moves.
void render_set_level(level_t const* lv, portal_t const portals[2]);

// Draw the frame into `target` (the framebuffer, or the half-size layer):
// the chamber, its cubes, buttons and doors, and the views through the
// portals, seen by the player.
void render_frame(pax_buf_t* target, game_t const* g);

void render_set_portal_depth(int depth);
// A ghost to draw (ghost.h): Chell's figure, glowing, where `pl` is; NULL for none.
void render_set_ghost(player_t const* pl);  // 1 .. RENDER_PORTAL_DEPTH_MAX
// Seconds since the start, for what moves by itself: the goo drifts, the
// fizzlers' streaks fall.
void render_set_time(float seconds);
int  render_portal_depth(void);

// Passes drawn and triangles submitted by the last frame, for the HUD.
void render_stats(int* passes, int* tris);
// The chamber's rectangles as meshed now (for the tests: render.c meshes
// only the slices a change touches, and must give what level_mesh() does).
int  render_quads(mquad_t const** quads);
