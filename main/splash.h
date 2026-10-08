#pragma once
// The game's own splash, after the engine's: a corridor between two
// portals that face each other, so it goes on for ever, an energy pellet
// streaking down it -- into the blue, out of the orange -- and the title.
// Drawn by the game's own renderer: the world in render_frame, the title
// over it.

#include <stdbool.h>
#include "game.h"
#include "pax_gfx.h"

// Build it in `g`. False if it cannot be (then there is no splash).
bool  splash_start(game_t* g);
// A frame: the camera and the pellet moved on. False once it is over.
bool  splash_update(game_t* g, float dt);
// A key: on to the end.
void  splash_skip(void);
// How lit the world is (fading in and out), and the title over it.
float splash_lit(void);
void  splash_draw(pax_buf_t* fb);
