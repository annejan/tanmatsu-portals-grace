#pragma once
// main.c, for the parts of the app around the game (watch.c).

#include "game.h"

game_t* app_game(void);
// Chamber `index` loaded, fresh; its story told unless it was the last
// told. False, with a message, if it cannot be read.
bool    app_load_chamber(int index);
// The next chamber's story told even if it is the one just told.
void    app_tell_again(void);
// To the title screen, the chambers playing behind it.
void    app_to_title(void);
float   app_fps(void);
