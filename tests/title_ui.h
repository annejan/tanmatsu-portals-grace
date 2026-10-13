#pragma once
// The title screen as the badge draws it (tests/title_ui.c), for
// tests/host_title.c: main/menu.c's title on the engine's se_ui menu,
// main/hud.c's title words, over the attract mode's picture.

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

// Settings as they ship, the last chamber played `continue_id` ("" none).
void title_ui_init(char const* continue_id);
// main.c's to_title: menu_title, Continue as the settings say.
void title_ui_open(void);
bool title_ui_on_title(void);
bool title_ui_menu_music(void);
bool title_ui_half(void);
int  title_ui_portal_depth(void);
// A key: 'u' up, 'd' down, 'o' Enter, 'b' Esc -- on the next
// title_ui_update, as on_input's events reach menu_frame.
void title_ui_key(char key);
// menu_frame's menu_update.
void title_ui_update(void);
// `px`: DISPLAY_LOG_W x DISPLAY_LOG_H 0xAARRGGBB, the rasterizer's picture
// (with `quarter`, in its top-left quarter); out as the badge shows it --
// in RGB565, faded by `lit`, hud_draw_title and menu_draw over it.
void title_ui_frame(uint32_t* px, bool quarter, float lit, game_t const* g);
