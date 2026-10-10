#pragma once
// The badge's own HUD on host_movie's frames (tests/movie_hud.c).

#include <stdint.h>
#include "game.h"

void movie_hud_init(void);
// `px`: DISPLAY_LOG_W x DISPLAY_LOG_H, 0xAARRGGBB, the rasterizer's
// picture; out as the badge shows it -- in RGB565, faded by `lit` (1: not
// at all), main/hud.c's HUD drawn over it.
void movie_hud_frame(uint32_t* px, float lit, game_t const* g);
