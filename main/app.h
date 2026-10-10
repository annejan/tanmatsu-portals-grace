#pragma once
// main.c, for the parts of the app around the game (watch.c).

#include "game.h"
#include "lift.h"

game_t* app_game(void);
// Chamber `index` loaded, fresh; its story told unless it was the last
// told. False, with a message, if it cannot be read.
bool    app_load_chamber(int index);
// The next chamber's story told even if it is the one just told.
void    app_tell_again(void);
// To the title screen, the chambers playing behind it.
void    app_to_title(void);
float   app_fps(void);
// The last frame's costs: its update (the game, a re-mesh) and its render
// (the 3D and the fades), in microseconds, and the render's passes and
// triangles (Settings -> Frame times, watch.c).
void    app_frame_cost(int* update_us, int* render_us, int* passes, int* tris);
// The lift stations of the chamber in play.
lift_sites_t const* app_lift_sites(void);
