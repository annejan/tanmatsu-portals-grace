#include "menu.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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
    SCR_TITLE,
    SCR_PAUSE,
    SCR_SETTINGS,
    SCR_CONTROLS,
    SCR_CHAMBERS,
    SCR_RECORDINGS,
    SCR_TIMES,
    SCR_COUNT
} screen_t;

static screen_t s_scr;
static screen_t s_home;  // where a screen's Back goes: the title or the pause menu
static int      s_cursor[SCR_COUNT];
static int      s_chamber;  // the one being played (on the title: Continue's, or the first)
static uint32_t s_act;      // this frame's actions, from events
static int64_t  s_opened_us;

#define CHAMBERS_MAX CHAMBER_MAX
static char s_names[CHAMBERS_MAX][32];  // chamber names, read once when the list opens
// The recordings on the card, read when their list opens.
static char s_rec_ids[RECORDINGS_MAX][CHAMBER_ID_N];
static char s_rec_names[RECORDINGS_MAX][RECORDING_NAME_N];
static int  s_n_recs;

enum {
    A_UP    = 1,
    A_DOWN  = 2,
    A_LEFT  = 4,
    A_RIGHT = 8,
    A_OK    = 16,
    A_BACK  = 32
};

static level_t s_lv;  // for a chamber's name: parsing one is too big for the stack

static void go(screen_t s) {
    s_scr = s;
    if (s == SCR_RECORDINGS) {
        s_cursor[s] = 0;
        s_n_recs    = recording_list(RECORDING_DIR, s_rec_ids, s_rec_names);
    }
    if (s == SCR_CHAMBERS) {
        s_cursor[s] = s_chamber;
        // Each name means parsing a whole chamber file: once, not every frame.
        for (int i = 0; i < level_count() && i < CHAMBERS_MAX; i++) {
            if (!level_load(&s_lv, i)) snprintf(s_lv.name, sizeof(s_lv.name), "%s (broken)", chamber_id(i));
            snprintf(s_names[i], sizeof(s_names[i]), "%s", s_lv.name);
        }
    }
}

void menu_open(int current_chamber) {
    s_chamber           = current_chamber;
    s_home              = SCR_PAUSE;
    s_act               = 0;
    s_opened_us         = esp_timer_get_time();
    s_cursor[SCR_PAUSE] = 0;
    go(SCR_PAUSE);
}

// The title's rows: Continue only when there is a chamber to go back to.
typedef enum {
    T_CONTINUE,
    T_NEW,
    T_RECORD,
    T_CHAMBERS,
    T_WATCH,
    T_EDITOR,
    T_SETTINGS,
    T_CONTROLS,
    T_QUIT,
    T_COUNT
} title_row_t;
static char const* const s_title_labels[T_COUNT] = {
    "Continue",       "New game", "Record a run", "Chamber select",   "Watch a recording",
    "Chamber editor", "Settings", "Controls",     "Quit to launcher",
};

// Under the title: the game's version, from metadata/metadata.json.
#if __has_include("app_version.h")
#include "app_version.h"
#endif
#ifndef APP_VERSION
#define APP_VERSION ""
#endif

static se_menu_row_t s_title_rows[T_COUNT];
static title_row_t   s_title_of[T_COUNT];  // each row's item
static int           s_title_n;
static char          s_title_sub[48];
static char          s_continue_name[40];  // Continue's value: the chamber, cut to fit the panel

#define TITLE_PANEL_W  0.56f
#define TITLE_VALUE_DX 120.0f

// `name` into `out`, cut short with "..." where it would run past the
// title's panel: chamber files on the card may have long names.
static void fit_name(char* out, size_t n, char const* name) {
    float const room = TITLE_PANEL_W * DISPLAY_LOG_W - 2.0f * SE_UI_TEXT_INSET - SE_UI_CHEVRON_GUTTER - TITLE_VALUE_DX;
    snprintf(out, n, "%s", name);
    if (rendertext_size(NULL, SE_UI_ROW_TEXT_H, out).x <= room) return;
    for (size_t len = strlen(out); len > 0; len--) {
        snprintf(out, n, "%.*s...", (int)len - 1, name);
        if (rendertext_size(NULL, SE_UI_ROW_TEXT_H, out).x <= room) return;
    }
}

void menu_title(int continue_chamber) {
    bool const cont    = continue_chamber >= 0 && continue_chamber < level_count();
    s_chamber          = cont ? continue_chamber : 0;
    s_continue_name[0] = '\0';
    if (cont && level_load(&s_lv, continue_chamber)) fit_name(s_continue_name, sizeof(s_continue_name), s_lv.name);
    s_title_n = 0;
    for (int t = cont ? T_CONTINUE : T_NEW; t < T_COUNT; t++) {
        s_title_of[s_title_n] = (title_row_t)t;
        s_title_rows[s_title_n] =
            t == T_CONTINUE
                ? (se_menu_row_t){.label = s_title_labels[t], .kind = SE_MENU_VAL_TEXT, .value = s_continue_name}
                : (se_menu_row_t){.label = s_title_labels[t]};
        s_title_n++;
    }
    if (APP_VERSION[0])
        snprintf(s_title_sub, sizeof(s_title_sub), "for Tanmatsu  -  %s", APP_VERSION);
    else
        snprintf(s_title_sub, sizeof(s_title_sub), "for Tanmatsu");
    s_home              = SCR_TITLE;
    s_act               = 0;
    s_opened_us         = esp_timer_get_time();
    s_cursor[SCR_TITLE] = 0;  // Continue if there is one, else New game
    go(SCR_TITLE);
}

bool menu_on_title(void) {
    return s_scr != SCR_NONE && s_home == SCR_TITLE;
}

bool menu_title_shown(void) {
    return s_scr == SCR_TITLE;
}

void menu_close(void) {
    s_scr = SCR_NONE;
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

#define PAUSE_ROWS   10
#define PAUSE_RECORD 7  // its label: "Record from here", or "Stop recording"
static se_menu_row_t s_pause_rows[PAUSE_ROWS] = {
    {.label = "Resume"},
    {.label = "Restart chamber"},
    {.label = "Chamber select"},
    {.label = "Chamber editor"},
    {.label = "Settings"},
    {.label = "Controls"},
    {.label = "Watch a recording"},
    {.label = "Record from here"},
    {.label = "Title screen"},
    {.label = "Quit to launcher"},
};
static bool s_recording;

void menu_set_recording(bool on) {
    s_recording                      = on;
    s_pause_rows[PAUSE_RECORD].label = on ? "Stop recording" : "Record from here";
}

// A recording's times (menu_times): copied, as the strings may not last.
#define TIMES_VALUE_N 32
static char          s_t_name[RECORDING_NAME_N + 40];  // and its version
static char          s_t_label[RECORDING_MAX][32], s_t_value[RECORDING_MAX][TIMES_VALUE_N], s_t_total[24];
static int           s_t_n;
static screen_t      s_t_back;  // what was up under it, or SCR_NONE
static se_menu_row_t s_t_rows[RECORDING_MAX + 2];

enum {
    SET_GYRO,
    SET_HALF,
    SET_DEPTH,
    SET_MUSIC,
    SET_SFX,
    SET_VOICE,
    SET_LEDS,
    SET_FRAMES,
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
static se_menu_row_t s_rec_rows[RECORDINGS_MAX + 1];
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

// The title's panel (TITLE_PANEL_W): narrower, and only as high as its
// rows, so the chambers playing themselves behind it show round it. Its
// one value, Continue's chamber, sits just after the label.

static int build_rows(screen_t s, se_menu_def_t* def);

static int build(screen_t s, se_menu_def_t* def) {
    int const n       = build_rows(s, def);
    def->panel_w      = MENU_PANEL_W;
    def->panel_h      = MENU_PANEL_H;
    def->title_h      = MENU_TITLE_H;
    def->row_h        = MENU_ROW_H;
    def->value_dx     = MENU_VALUE_DX;
    def->visible_rows = n > MENU_ROWS ? MENU_ROWS : 0;
    if (s == SCR_TITLE) {
        // As se_menu_draw lays it out: 40 px to the title, the title and
        // 14, the subtitle and 16, the rows, and room below the last.
        def->panel_w  = TITLE_PANEL_W;
        def->panel_h  = (40.0f + MENU_TITLE_H + 14.0f + 34.0f + (float)n * MENU_ROW_H + 24.0f) / (float)DISPLAY_LOG_H;
        def->value_dx = TITLE_VALUE_DX;
    }
    return n;
}

static int build_rows(screen_t s, se_menu_def_t* def) {
    *def = (se_menu_def_t){0};
    switch (s) {
        case SCR_TITLE:
            def->title     = "PORTALS";
            def->subtitle  = s_title_sub;
            def->rows      = s_title_rows;
            def->row_count = s_title_n;
            return s_title_n;
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
            s_set_rows[SET_VOICE] =
                (se_menu_row_t){.label = "GLaDOS voice", .kind = SE_MENU_VAL_CHECK, .checked = settings_voice()};
            s_set_rows[SET_LEDS] =
                (se_menu_row_t){.label = "Portal LEDs", .kind = SE_MENU_VAL_CHECK, .checked = settings_leds()};
            s_set_rows[SET_FRAMES] =
                (se_menu_row_t){.label = "Frame times", .kind = SE_MENU_VAL_CHECK, .checked = settings_frame_times()};
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
        case SCR_TIMES:
            for (int i = 0; i < s_t_n; i++)
                s_t_rows[i] = (se_menu_row_t){.label = s_t_label[i], .kind = SE_MENU_VAL_TEXT, .value = s_t_value[i]};
            s_t_rows[s_t_n]     = (se_menu_row_t){.label = "Total", .kind = SE_MENU_VAL_TEXT, .value = s_t_total};
            s_t_rows[s_t_n + 1] = (se_menu_row_t){.label = "Back"};
            def->title          = "TIMES";
            def->subtitle       = s_t_name;
            def->rows           = s_t_rows;
            def->row_count      = s_t_n + 2;
            return s_t_n + 2;
        case SCR_RECORDINGS: {
            for (int i = 0; i < s_n_recs; i++) s_rec_rows[i] = (se_menu_row_t){.label = s_rec_names[i]};
            s_rec_rows[s_n_recs] = (se_menu_row_t){.label = "Back"};
            def->title           = "RECORDINGS";
            def->subtitle =
                s_n_recs > 0 ? "Played back as this badge plays them, timed" : "None on the card: " RECORDING_DIR;
            def->rows      = s_rec_rows;
            def->row_count = s_n_recs + 1;
            def->hint      = "Enter: watch   Esc: stop watching";
            return s_n_recs + 1;
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
        case SCR_TITLE:
            // Esc does nothing here: the game has only just begun.
            if (r != SE_MENU_RESULT_ACTIVATED) break;
            switch (s_title_of[cur]) {
                case T_CONTINUE:
                    s_scr       = SCR_NONE;
                    cmd.kind    = MENU_CMD_CHAMBER;
                    cmd.chamber = s_chamber;
                    break;
                case T_RECORD:
                    s_scr       = SCR_NONE;
                    cmd.kind    = MENU_CMD_RECORD;
                    cmd.chamber = -1;
                    break;
                case T_NEW:
                    s_scr    = SCR_NONE;
                    cmd.kind = MENU_CMD_NEW_GAME;
                    break;
                case T_CHAMBERS:
                    go(SCR_CHAMBERS);
                    break;
                case T_WATCH:
                    go(SCR_RECORDINGS);
                    break;
                case T_EDITOR:
                    s_scr       = SCR_NONE;
                    cmd.kind    = MENU_CMD_EDITOR;
                    cmd.chamber = s_chamber;
                    break;
                case T_SETTINGS:
                    go(SCR_SETTINGS);
                    break;
                case T_CONTROLS:
                    go(SCR_CONTROLS);
                    break;
                default:
                    cmd.kind = MENU_CMD_QUIT;
                    break;
            }
            break;

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
                        s_scr       = SCR_NONE;
                        cmd.kind    = MENU_CMD_EDITOR;
                        cmd.chamber = s_chamber;
                        break;
                    case 4:
                        go(SCR_SETTINGS);
                        break;
                    case 5:
                        go(SCR_CONTROLS);
                        break;
                    case 6:
                        go(SCR_RECORDINGS);
                        break;
                    case PAUSE_RECORD:
                        s_scr       = SCR_NONE;
                        cmd.kind    = s_recording ? MENU_CMD_RECORD_STOP : MENU_CMD_RECORD;
                        cmd.chamber = s_chamber;
                        break;
                    case 8:
                        s_scr    = SCR_NONE;
                        cmd.kind = MENU_CMD_TITLE;
                        break;
                    case 9:
                        cmd.kind = MENU_CMD_QUIT;
                        break;
                }
            }
            break;

        case SCR_SETTINGS: {
            int const d = r == SE_MENU_RESULT_INCREMENT ? 10 : r == SE_MENU_RESULT_DECREMENT ? -10 : 0;
            if (r == SE_MENU_RESULT_BACK || (r == SE_MENU_RESULT_ACTIVATED && cur == SET_BACK)) {
                go(s_home);
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
            } else if (cur == SET_VOICE && (r == SE_MENU_RESULT_ACTIVATED || (act & (A_LEFT | A_RIGHT)))) {
                settings_set_voice(!settings_voice());
                sound_set_voice(settings_voice());
            } else if (cur == SET_LEDS && (r == SE_MENU_RESULT_ACTIVATED || (act & (A_LEFT | A_RIGHT)))) {
                settings_set_leds(!settings_leds());
            } else if (cur == SET_FRAMES && (r == SE_MENU_RESULT_ACTIVATED || (act & (A_LEFT | A_RIGHT)))) {
                settings_set_frame_times(!settings_frame_times());
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
                go(s_home);
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
                go(s_home);
            } else if (r == SE_MENU_RESULT_ACTIVATED) {
                s_scr       = SCR_NONE;
                cmd.kind    = MENU_CMD_CHAMBER;
                cmd.chamber = cur;
            }
            break;

        case SCR_TIMES:
            if (r == SE_MENU_RESULT_BACK || (r == SE_MENU_RESULT_ACTIVATED && cur == def.row_count - 1)) {
                if (s_t_back == SCR_NONE) {
                    s_scr    = SCR_NONE;
                    cmd.kind = MENU_CMD_RESUME;
                } else {
                    go(s_t_back);
                }
            }
            break;

        case SCR_RECORDINGS:
            if (r == SE_MENU_RESULT_BACK || (r == SE_MENU_RESULT_ACTIVATED && cur == def.row_count - 1)) {
                go(s_home);
            } else if (r == SE_MENU_RESULT_ACTIVATED) {
                s_scr         = SCR_NONE;
                cmd.kind      = MENU_CMD_WATCH;
                cmd.recording = s_rec_ids[cur];
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

void menu_times(char const* name, int n, char const* const* ids, char const* const* values, char const* total) {
    snprintf(s_t_name, sizeof(s_t_name), "%s", name);
    s_t_n = n < RECORDING_MAX ? n : RECORDING_MAX;
    for (int i = 0; i < s_t_n; i++) {
        // The chamber's name, if it is still on the list.
        int const at = chamber_find(ids[i]);
        if (at < 0 || !level_load(&s_lv, at)) snprintf(s_lv.name, sizeof(s_lv.name), "%s", ids[i]);
        snprintf(s_t_label[i], sizeof(s_t_label[i]), "%s", s_lv.name);
        snprintf(s_t_value[i], sizeof(s_t_value[i]), "%s", values[i]);
    }
    snprintf(s_t_total, sizeof(s_t_total), "%s", total);
    s_t_back = s_scr;
    if (s_scr == SCR_NONE) s_home = SCR_PAUSE;  // no menu up: Back resumes, and its screens go to the pause menu
    s_act               = 0;
    s_opened_us         = esp_timer_get_time();
    s_cursor[SCR_TIMES] = s_t_n;  // on the total
    go(SCR_TIMES);
}
