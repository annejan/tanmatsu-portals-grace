#pragma once
// The pause menu and its screens, on the engine's se_ui list menus:
// pause, settings, controls (rebinding through se_ui_capture_key) and
// chamber select.

#include <stdbool.h>
#include "bsp/input.h"
#include "pax_gfx.h"

typedef enum {
    MENU_CMD_NONE = 0,
    MENU_CMD_RESUME,
    MENU_CMD_RESTART,
    MENU_CMD_CHAMBER,  // load menu_cmd_t.chamber
    MENU_CMD_QUIT,
} menu_cmd_kind_t;

typedef struct {
    menu_cmd_kind_t kind;
    int             chamber;
} menu_cmd_t;

void menu_open(int current_chamber);
bool menu_active(void);
// Every input event while the menu is open; also Esc, to open it.
void menu_event(bsp_input_event_t const* ev);
// True if `ev` is the key that opens the menu.
bool menu_is_open_key(bsp_input_event_t const* ev);
// Once a frame while open: act on the keys this frame brought.
menu_cmd_t menu_update(void);
void       menu_draw(pax_buf_t* fb);
