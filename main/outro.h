#pragma once
// A desk story's outro (pack.h `outro:`): its chamber walked through, the
// walk being the chamber's solution, the lights low; a fade to black; then
// the cards of the desk's outro.txt (desk.h) typed out, the last of them
// the reveal.

#include <stdbool.h>
#include "desk.h"
#include "game.h"
#include "pax_gfx.h"

// Into chamber `chamber` in `g`, its solution the walk. False if it cannot
// be read, or has no solution.
bool  outro_start(game_t* g, int chamber, desk_data_t const* d);
// A frame of it: the walk's events in `events`. False once it is over.
bool  outro_update(game_t* g, float dt, int* events);
// A key: the walk cut short, a card typed out or the next one.
void  outro_skip(void);
// Whether the world is to be drawn (the walk and the fade), and how lit.
bool  outro_world(void);
float outro_lit(void);
// The cards, over a black screen, once the walk is done.
void  outro_draw(pax_buf_t* fb);
