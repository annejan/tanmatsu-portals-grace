#include "menu.h"
#include <stdint.h>
#include <stdio.h>
#include "chamber.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "input.h"
#include "level.h"
#include "settings.h"
#include "sound.h"
#include "synthengine3d.h"

static char const TAG[] = "menu";

typedef enum {
    SCR_NONE = 0,
    SCR_PAUSE,
    SCR_SETTINGS,
    SCR_CONTROLS,
    SCR_CHAMBERS,
    SCR_COUNT
} screen_t;

static screen_t s_scr;
static int      s_cursor[SCR_COUNT];
static int      s_chamber;  // the one being played
static uint32_t s_act;      // this frame's actions, from events
static int64_t  s_opened_us;

#define CHAMBERS_MAX CHAMBER_MAX
static char s_names[CHAMBERS_MAX][32];  // chamber names, read once when the list opens

enum {
    A_UP    = 1,
    A_DOWN  = 2,
    A_LEFT  = 4,
    A_RIGHT = 8,
    A_OK    = 16,
    A_BACK  = 32
};

static void go(screen_t s) {
    s_scr = s;
    if (s == SCR_CHAMBERS) {
        s_cursor[s] = s_chamber;
        // Each name means parsing a whole chamber file: once, not every frame.
        static level_t lv;
        for (int i = 0; i < level_count() && i < CHAMBERS_MAX; i++) {
            if (!level_load(&lv, i)) snprintf(lv.name, sizeof(lv.name), "%s (broken)", chamber_id(i));
            snprintf(s_names[i], sizeof(s_names[i]), "%s", lv.name);
        }
    }
}

void menu_open(int current_chamber) {
    s_chamber           = current_chamber;
    s_act               = 0;
    s_opened_us         = esp_timer_get_time();
    s_cursor[SCR_PAUSE] = 0;
    go(SCR_PAUSE);
}

bool menu_active(void) {
    return s_scr != SCR_NONE;
}

bool menu_is_open_key(bsp_input_event_t const* ev) {
    if (ev->type == INPUT_EVENT_TYPE_SCANCODE) return ev->args_scancode.scancode == BSP_INPUT_SCANCODE_ESC;
    if (ev->type == INPUT_EVENT_TYPE_NAVIGATION)
        return ev->args_navigation.state && ev->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_ESC;
    return false;
}

// The built-in keyboard sends a cursor key both as a scancode and as a
// navigation event; collected into one bit set per frame, it counts once.
void menu_event(bsp_input_event_t const* ev) {
    if (s_scr == SCR_NONE) return;
    // The built-in keyboard sends Esc twice, as a scancode and as a
    // navigation key, in the same frame. The first opened this menu; the
    // second must not close it again before it was ever drawn.
    if (esp_timer_get_time() - s_opened_us < 250000 && menu_is_open_key(ev)) return;
    if (ev->type == INPUT_EVENT_TYPE_SCANCODE) {
        uint16_t const sc = ev->args_scancode.scancode;
        if ((sc & BSP_INPUT_SCANCODE_RELEASE_MODIFIER) != 0) return;
        switch (sc) {
            case BSP_INPUT_SCANCODE_ESC:
            case BSP_INPUT_SCANCODE_BACKSPACE:
                s_act |= A_BACK;
                break;
            case BSP_INPUT_SCANCODE_ENTER:
            case BSP_INPUT_SCANCODE_SPACE:
            case BSP_INPUT_SCANCODE_ESCAPED_KPENTER:
                s_act |= A_OK;
                break;
            case BSP_INPUT_SCANCODE_W:
            case BSP_INPUT_SCANCODE_ESCAPED_GREY_UP:
                s_act |= A_UP;
                break;
            case BSP_INPUT_SCANCODE_S:
            case BSP_INPUT_SCANCODE_ESCAPED_GREY_DOWN:
                s_act |= A_DOWN;
                break;
            case BSP_INPUT_SCANCODE_A:
            case BSP_INPUT_SCANCODE_ESCAPED_GREY_LEFT:
                s_act |= A_LEFT;
                break;
            case BSP_INPUT_SCANCODE_D:
            case BSP_INPUT_SCANCODE_ESCAPED_GREY_RIGHT:
                s_act |= A_RIGHT;
                break;
            default:
                break;
        }
    } else if (ev->type == INPUT_EVENT_TYPE_NAVIGATION && ev->args_navigation.state) {
        switch (ev->args_navigation.key) {
            case BSP_INPUT_NAVIGATION_KEY_ESC:
            case BSP_INPUT_NAVIGATION_KEY_BACKSPACE:
                s_act |= A_BACK;
                break;
            case BSP_INPUT_NAVIGATION_KEY_RETURN:
                s_act |= A_OK;
                break;
            case BSP_INPUT_NAVIGATION_KEY_UP:
                s_act |= A_UP;
                break;
            case BSP_INPUT_NAVIGATION_KEY_DOWN:
                s_act |= A_DOWN;
                break;
            case BSP_INPUT_NAVIGATION_KEY_LEFT:
                s_act |= A_LEFT;
                break;
            case BSP_INPUT_NAVIGATION_KEY_RIGHT:
                s_act |= A_RIGHT;
                break;
            default:
                break;
        }
    }
}

// --- The screens --------------------------------------------------------

#define PAUSE_ROWS 7
static se_menu_row_t const s_pause_rows[PAUSE_ROWS] = {
    {.label = "Resume"},   {.label = "Restart chamber"}, {.label = "Chamber select"},   {.label = "Chamber editor"},
    {.label = "Settings"}, {.label = "Controls"},        {.label = "Quit to launcher"},
};

enum {
    SET_GYRO,
    SET_HALF,
    SET_DEPTH,
    SET_MUSIC,
    SET_SFX,
    SET_LEDS,
    SET_VOLUME,
    SET_SCREEN,
    SET_KEYS,
    SET_BACK,
    SET_ROWS
};
#define CONTROLS_ROWS (ACT_COUNT + 2)  // every action, reset, back

static se_menu_row_t s_set_rows[SET_ROWS];
static se_menu_row_t s_ctl_rows[CONTROLS_ROWS];
static se_menu_row_t s_ch_rows[CHAMBERS_MAX + 1];
static char          s_depth_text[8];

static void draw_key(pax_buf_t* fb, float x, float y, float h, pax_col_t col, void* ctx) {
    char buf[24];
    rendertext_draw(fb, col, NULL, h, x, y, input_key_name(input_key((action_t)(uintptr_t)ctx), buf, sizeof(buf)));
}

// Every screen's layout. The engine's defaults (a panel 70% of the
// screen high, 44 px rows) hold four and a half rows; these hold nine,
// and a longer list scrolls (row text height: SE_UI_ROW_TEXT_H in
// CMakeLists.txt).
#define MENU_PANEL_W  0.70f
#define MENU_PANEL_H  0.92f
#define MENU_TITLE_H  28.0f
#define MENU_ROW_H    30.0f
#define MENU_ROWS     9
// Labels to values: the longest label ("Quarter resolution") is about
// 220 px at 20 px text, and a slider and its "NN%" still end inside the
// 560 px panel. At 0 the values were drawn on top of their labels.
#define MENU_VALUE_DX 270.0f

static int build_rows(screen_t s, se_menu_def_t* def);

static int build(screen_t s, se_menu_def_t* def) {
    int const n       = build_rows(s, def);
    def->panel_w      = MENU_PANEL_W;
    def->panel_h      = MENU_PANEL_H;
    def->title_h      = MENU_TITLE_H;
    def->row_h        = MENU_ROW_H;
    def->value_dx     = MENU_VALUE_DX;
    def->visible_rows = n > MENU_ROWS ? MENU_ROWS : 0;
    return n;
}

static int build_rows(screen_t s, se_menu_def_t* def) {
    *def = (se_menu_def_t){0};
    switch (s) {
        case SCR_PAUSE:
            def->title     = "PORTALS";
            def->subtitle  = "Paused";
            def->rows      = s_pause_rows;
            def->row_count = PAUSE_ROWS;
            def->hint      = "Enter: choose   Esc: back to the game";
            return PAUSE_ROWS;
        case SCR_SETTINGS:
            snprintf(s_depth_text, sizeof(s_depth_text), "%d", settings_portal_depth());
            s_set_rows[SET_GYRO] =
                (se_menu_row_t){.label = "Gyroscope look", .kind = SE_MENU_VAL_CHECK, .checked = settings_gyro()};
            s_set_rows[SET_HALF] = (se_menu_row_t){
                .label = "Quarter resolution", .kind = SE_MENU_VAL_CHECK, .checked = settings_half_res()};
            s_set_rows[SET_DEPTH] =
                (se_menu_row_t){.label = "Portal depth", .kind = SE_MENU_VAL_TEXT, .value = s_depth_text};
            s_set_rows[SET_MUSIC] =
                (se_menu_row_t){.label = "Music", .kind = SE_MENU_VAL_CHECK, .checked = settings_music()};
            s_set_rows[SET_SFX] =
                (se_menu_row_t){.label = "Sound effects", .kind = SE_MENU_VAL_CHECK, .checked = settings_effects()};
            s_set_rows[SET_LEDS] =
                (se_menu_row_t){.label = "Portal LEDs", .kind = SE_MENU_VAL_CHECK, .checked = settings_leds()};
            s_set_rows[SET_VOLUME] =
                (se_menu_row_t){.label = "Volume", .kind = SE_MENU_VAL_RANGE, .range_pct = se_hw_get_volume()};
            s_set_rows[SET_SCREEN] = (se_menu_row_t){
                .label = "Screen brightness", .kind = SE_MENU_VAL_RANGE, .range_pct = se_hw_get_display_brightness()};
            s_set_rows[SET_KEYS] = (se_menu_row_t){
                .label = "Keyboard light", .kind = SE_MENU_VAL_RANGE, .range_pct = se_hw_get_keyboard_brightness()};
            s_set_rows[SET_BACK] = (se_menu_row_t){.label = "Back"};
            def->title           = "SETTINGS";
            def->rows            = s_set_rows;
            def->row_count       = SET_ROWS;
            def->hint            = "Enter: toggle   Left / Right: adjust";
            return SET_ROWS;
        case SCR_CONTROLS:
            for (int i = 0; i < ACT_COUNT; i++)
                s_ctl_rows[i] = (se_menu_row_t){.label      = input_action_label((action_t)i),
                                                .kind       = SE_MENU_VAL_CUSTOM,
                                                .draw_value = draw_key,
                                                .ctx        = (void*)(uintptr_t)i};
            s_ctl_rows[ACT_COUNT]     = (se_menu_row_t){.label = "Reset to defaults"};
            s_ctl_rows[ACT_COUNT + 1] = (se_menu_row_t){.label = "Back"};
            def->title                = "CONTROLS";
            def->rows                 = s_ctl_rows;
            def->row_count            = CONTROLS_ROWS;
            def->hint                 = "Enter: press the new key   Esc: back";
            return CONTROLS_ROWS;
        case SCR_CHAMBERS: {
            int n = level_count();
            if (n > CHAMBERS_MAX) n = CHAMBERS_MAX;
            for (int i = 0; i < n; i++)
                s_ch_rows[i] =
                    (se_menu_row_t){.label = s_names[i], .kind = SE_MENU_VAL_RADIO, .checked = i == s_chamber};
            s_ch_rows[n]   = (se_menu_row_t){.label = "Back"};
            def->title     = "CHAMBERS";
            def->rows      = s_ch_rows;
            def->row_count = n + 1;
            return n + 1;
        }
        default:
            return 0;
    }
}

static uint8_t step_pct(uint8_t v, int d) {
    int const n = (int)v + d;
    return (uint8_t)(n < 0 ? 0 : n > 100 ? 100 : n);
}

menu_cmd_t menu_update(void) {
    menu_cmd_t cmd = {0};
    if (s_scr == SCR_NONE) return cmd;
    uint32_t const act = s_act;
    s_act              = 0;

    se_menu_def_t def;
    build(s_scr, &def);
    se_menu_t        m = {.def = &def, .cursor = s_cursor[s_scr]};
    se_menu_result_t r = SE_MENU_RESULT_NONE;
    if (act & A_UP) se_menu_input(&m, SE_MENU_ACT_UP);
    if (act & A_DOWN) se_menu_input(&m, SE_MENU_ACT_DOWN);
    if (act & A_LEFT) r = se_menu_input(&m, SE_MENU_ACT_LEFT);
    if (act & A_RIGHT) r = se_menu_input(&m, SE_MENU_ACT_RIGHT);
    if (act & A_OK) r = se_menu_input(&m, SE_MENU_ACT_ACTIVATE);
    if (act & A_BACK) r = se_menu_input(&m, SE_MENU_ACT_BACK);
    int const cur = m.cursor;
    if (cur != s_cursor[s_scr] || r == SE_MENU_RESULT_ACTIVATED) sound_play(SND_MENU);
    s_cursor[s_scr] = cur;

    switch (s_scr) {
        case SCR_PAUSE:
            if (r == SE_MENU_RESULT_BACK) {
                s_scr    = SCR_NONE;
                cmd.kind = MENU_CMD_RESUME;
            } else if (r == SE_MENU_RESULT_ACTIVATED) {
                switch (cur) {
                    case 0:
                        s_scr    = SCR_NONE;
                        cmd.kind = MENU_CMD_RESUME;
                        break;
                    case 1:
                        s_scr    = SCR_NONE;
                        cmd.kind = MENU_CMD_RESTART;
                        break;
                    case 2:
                        go(SCR_CHAMBERS);
                        break;
                    case 3:
                        s_scr    = SCR_NONE;
                        cmd.kind = MENU_CMD_EDITOR;
                        break;
                    case 4:
                        go(SCR_SETTINGS);
                        break;
                    case 5:
                        go(SCR_CONTROLS);
                        break;
                    case 6:
                        cmd.kind = MENU_CMD_QUIT;
                        break;
                }
            }
            break;

        case SCR_SETTINGS: {
            int const d = r == SE_MENU_RESULT_INCREMENT ? 10 : r == SE_MENU_RESULT_DECREMENT ? -10 : 0;
            if (r == SE_MENU_RESULT_BACK || (r == SE_MENU_RESULT_ACTIVATED && cur == SET_BACK)) {
                go(SCR_PAUSE);
            } else if (cur == SET_GYRO && (r == SE_MENU_RESULT_ACTIVATED || (act & (A_LEFT | A_RIGHT)))) {
                settings_set_gyro(!settings_gyro());
            } else if (cur == SET_HALF && (r == SE_MENU_RESULT_ACTIVATED || (act & (A_LEFT | A_RIGHT)))) {
                settings_set_half_res(!settings_half_res());
            } else if (cur == SET_MUSIC && (r == SE_MENU_RESULT_ACTIVATED || (act & (A_LEFT | A_RIGHT)))) {
                settings_set_music(!settings_music());
                sound_set_music(settings_music());
            } else if (cur == SET_SFX && (r == SE_MENU_RESULT_ACTIVATED || (act & (A_LEFT | A_RIGHT)))) {
                settings_set_effects(!settings_effects());
                sound_set_effects(settings_effects());
            } else if (cur == SET_LEDS && (r == SE_MENU_RESULT_ACTIVATED || (act & (A_LEFT | A_RIGHT)))) {
                settings_set_leds(!settings_leds());
            } else if (cur == SET_DEPTH && (r == SE_MENU_RESULT_ACTIVATED || (act & A_RIGHT))) {
                settings_set_portal_depth(settings_portal_depth() % 3 + 1);
            } else if (cur == SET_DEPTH && (act & A_LEFT)) {
                settings_set_portal_depth((settings_portal_depth() + 1) % 3 + 1);
            } else if (d != 0 && cur == SET_VOLUME) {
                se_hw_set_volume(step_pct(se_hw_get_volume(), d));
            } else if (d != 0 && cur == SET_SCREEN) {
                // Not all the way down: a black screen is hard to find a menu on.
                uint8_t const v = step_pct(se_hw_get_display_brightness(), d);
                se_hw_set_display_brightness(v < 10 ? 10 : v);
            } else if (d != 0 && cur == SET_KEYS) {
                se_hw_set_keyboard_brightness(step_pct(se_hw_get_keyboard_brightness(), d));
            }
            break;
        }

        case SCR_CONTROLS:
            if (r == SE_MENU_RESULT_BACK || (r == SE_MENU_RESULT_ACTIVATED && cur == ACT_COUNT + 1)) {
                go(SCR_PAUSE);
            } else if (r == SE_MENU_RESULT_ACTIVATED && cur == ACT_COUNT) {
                input_reset_defaults();
            } else if (r == SE_MENU_RESULT_ACTIVATED && cur < ACT_COUNT) {
                // The engine's blocking "press a key": it takes any key, Esc
                // included, so there is no cancel -- pressing the key the
                // action already has keeps it.
                uint16_t const sc = se_ui_capture_key(input_action_label((action_t)cur));
                if (sc != 0) {
                    char name[24];
                    input_bind((action_t)cur, sc);
                    ESP_LOGI(TAG, "%s bound to %s", input_action_label((action_t)cur),
                             input_key_name(sc, name, sizeof(name)));
                }
                s_act = 0;  // the key just pressed is not also a menu action
            }
            break;

        case SCR_CHAMBERS:
            if (r == SE_MENU_RESULT_BACK || (r == SE_MENU_RESULT_ACTIVATED && cur == def.row_count - 1)) {
                go(SCR_PAUSE);
            } else if (r == SE_MENU_RESULT_ACTIVATED) {
                s_scr       = SCR_NONE;
                cmd.kind    = MENU_CMD_CHAMBER;
                cmd.chamber = cur;
            }
            break;

        default:
            break;
    }
    return cmd;
}

void menu_draw(pax_buf_t* fb) {
    if (s_scr == SCR_NONE) return;
    se_menu_def_t def;
    build(s_scr, &def);
    se_menu_t const m = {.def = &def, .cursor = s_cursor[s_scr]};
    se_menu_draw(&m, fb);
}
