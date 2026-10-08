#include "deskview.h"
#include <string.h>
#include "esp_timer.h"
#include "pax_fonts.h"
#include "se_text.h"
#include "synthengine3d.h"

#define TEXT_H    14.0f  // the font's size: 10 x 19 px a cell
#define ROW_H     19.0f
#define GREEN     0xFF7CFF8Au
#define DIM       0xFF2E6B38u
#define SCREEN_BG 0xFF050806u

// Space comes as a navigation key and may come as a character too: one
// of the two, not both.
static int64_t s_space_us;

static desk_action_t space(desk_t* k) {
    int64_t const now = esp_timer_get_time();
    if (now - s_space_us < 40000) return DESK_NONE;
    s_space_us = now;
    return desk_key(k, DK_CHAR, ' ');
}

desk_action_t deskview_event(desk_t* k, bsp_input_event_t const* ev) {
    if (ev->type == INPUT_EVENT_TYPE_KEYBOARD) {
        // Text: printable ASCII, not with Ctrl or Alt.
        bsp_input_event_args_keyboard_t const* kb = &ev->args_keyboard;
        if (kb->modifiers & (BSP_INPUT_MODIFIER_CTRL | BSP_INPUT_MODIFIER_ALT_L)) return DESK_NONE;
        char const c = kb->utf8[0] ? kb->utf8[0] : kb->ascii;
        if (c == ' ') return space(k);
        if (c > ' ' && c < 0x7F && (kb->utf8[0] == '\0' || kb->utf8[1] == '\0')) return desk_key(k, DK_CHAR, c);
        return DESK_NONE;
    }
    // Everything else from the navigation keys, as they go down (Esc also
    // comes as a scancode: that one is passed over).
    if (ev->type != INPUT_EVENT_TYPE_NAVIGATION || !ev->args_navigation.state) return DESK_NONE;
    switch (ev->args_navigation.key) {
        case BSP_INPUT_NAVIGATION_KEY_RETURN:
            return desk_key(k, DK_ENTER, 0);
        case BSP_INPUT_NAVIGATION_KEY_BACKSPACE:
            return desk_key(k, DK_BACK, 0);
        case BSP_INPUT_NAVIGATION_KEY_ESC:
            return desk_key(k, DK_ESC, 0);
        case BSP_INPUT_NAVIGATION_KEY_UP:
            return desk_key(k, DK_UP, 0);
        case BSP_INPUT_NAVIGATION_KEY_DOWN:
            return desk_key(k, DK_DOWN, 0);
        case BSP_INPUT_NAVIGATION_KEY_LEFT:
            return desk_key(k, DK_LEFT, 0);
        case BSP_INPUT_NAVIGATION_KEY_RIGHT:
            return desk_key(k, DK_RIGHT, 0);
        case BSP_INPUT_NAVIGATION_KEY_PGUP:
            return desk_key(k, DK_PGUP, 0);
        case BSP_INPUT_NAVIGATION_KEY_PGDN:
            return desk_key(k, DK_PGDN, 0);
        case BSP_INPUT_NAVIGATION_KEY_TAB:
            return desk_key(k, DK_TAB, 0);
        case BSP_INPUT_NAVIGATION_KEY_SPACE_L:
        case BSP_INPUT_NAVIGATION_KEY_SPACE_M:
        case BSP_INPUT_NAVIGATION_KEY_SPACE_R:
            return space(k);
        default:
            return DESK_NONE;
    }
}

void deskview_draw(pax_buf_t* fb, desk_t const* k) {
    static char rows[DESK_ROWS][DESK_COLS + 1];
    int         cur_row, cur_col, hl;
    desk_screen(k, rows, &cur_row, &cur_col, &hl);
    pax_background(fb, SCREEN_BG);
    // Cells as wide as the font's, the grid in the middle.
    static float cell_w;
    if (cell_w <= 0.0f) cell_w = rendertext_size(pax_font_sky_mono, TEXT_H, "M").x;
    float const x0 = ((float)DISPLAY_LOG_W - cell_w * DESK_COLS) * 0.5f;
    float const y0 = ((float)DISPLAY_LOG_H - ROW_H * DESK_ROWS) * 0.5f;
    for (int r = 0; r < DESK_ROWS; r++) {
        float const y = y0 + ROW_H * (float)r;
        if (r == hl) {
            pax_simple_rect(fb, GREEN, x0, y, cell_w * DESK_COLS, ROW_H);
            if (rows[r][0]) rendertext_draw(fb, SCREEN_BG, pax_font_sky_mono, TEXT_H, x0, y + 2, rows[r]);
        } else if (rows[r][0]) {
            rendertext_draw(fb, GREEN, pax_font_sky_mono, TEXT_H, x0, y + 2, rows[r]);
        }
    }
    // The cursor, blinking: a block under the character.
    if (cur_row >= 0 && (esp_timer_get_time() / 500000) % 2 == 0)
        pax_simple_rect(fb, DIM, x0 + cell_w * (float)cur_col, y0 + ROW_H * (float)cur_row + ROW_H - 4, cell_w, 3);
}
