#include "editor.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "chamber.h"
#include "draft.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "gl_input.h"
#include "pax_fonts.h"
#include "pax_text.h"
#include "sound.h"
#include "synthengine3d.h"

static char const TAG[] = "editor";

#define REPEAT_FIRST 0.30f  // a held key moves the cursor again after this
#define REPEAT_NEXT  0.07f  // ... and then this often

static bool     s_active;
static draft_t  s_d;
static bool     s_new;  // s_d's file is not on the card yet: saving must not overwrite one
static char     s_dir[96];
static int      s_x, s_z, s_y = 1;  // the cursor, and the layer shown
static char     s_brush = 'W';
static char     s_door = 'a', s_button = '1';
static bool     s_painting;    // Space held: paint as the cursor moves
static bool     s_anchor_set;  // B pressed once: a box from here
static int      s_ax, s_az;
static float    s_held_t[4];  // per direction: how long it has been held
static float    s_next_t[4];  // ... and when it moves again
static int64_t  s_menu_opened_us;
static char     s_msg[96];
static float    s_msg_t;
static bool     s_menu;  // the editor's own menu is open
static int      s_menu_cursor;
static uint32_t s_act;  // menu actions this frame, from events
static bool     s_want_test, s_want_quit;
static level_t  s_level;  // the parsed chamber, for a play-test

enum {
    A_UP    = 1,
    A_DOWN  = 2,
    A_LEFT  = 4,
    A_RIGHT = 8,
    A_OK    = 16,
    A_BACK  = 32
};

static void say(char const* fmt, char const* arg) {
    snprintf(s_msg, sizeof(s_msg), fmt, arg);
    s_msg_t = 4.0f;
}

level_t const* editor_level(void) {
    return &s_level;
}

void editor_resume(void) {
    s_active   = true;
    s_painting = false;
}

static void fresh_id(char* out, size_t n, char const* base) {
    draft_fresh_id(s_dir, base, out, n);
}

void editor_open(int index, char const* chamber_dir) {
    snprintf(s_dir, sizeof(s_dir), "%s", chamber_dir);
    char id[CHAMBER_ID_N];
    char err[96];
    if (index >= 0 && index < chamber_count()) {
        bool const builtin = index < chamber_builtin_n();
        if (builtin)
            fresh_id(id, sizeof(id), chamber_id(index));
        else
            snprintf(id, sizeof(id), "%s", chamber_id(index));
        s_new = builtin;
        if (!draft_from_text(&s_d, id, chamber_text(index), err, sizeof(err))) {
            // Never an empty chamber under the name of a file that is there.
            ESP_LOGW(TAG, "%s: %s", chamber_id(index), err);
            fresh_id(id, sizeof(id), NULL);
            draft_new(&s_d, id, 12, 6, 12);
            s_new = true;
            say("Cannot edit that one: %s", err);
        } else if (builtin) {
            say("A copy: saved as %s.txt", id);
        }
    } else {
        fresh_id(id, sizeof(id), NULL);
        draft_new(&s_d, id, 12, 6, 12);
        s_new = true;
        say("New chamber %s", id);
    }
    s_x      = s_d.w / 2;
    s_z      = s_d.d / 2;
    s_y      = s_d.h > 1 ? 1 : 0;
    s_active = true;
    s_menu = s_painting = s_anchor_set = false;
}

// --- Editing ---------------------------------------------------------------

static void paint_here(void) {
    draft_paint(&s_d, s_x, s_y, s_z, s_brush);
}

static void box_fill(void) {
    int const x0 = s_ax < s_x ? s_ax : s_x, x1 = s_ax < s_x ? s_x : s_ax;
    int const z0 = s_az < s_z ? s_az : s_z, z1 = s_az < s_z ? s_z : s_az;
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            if (s_brush != 'S') draft_paint(&s_d, x, s_y, z, s_brush);
}

static void move(int dx, int dz) {
    int const nx = s_x + dx, nz = s_z + dz;
    if (nx < 0 || nz < 0 || nx >= s_d.w || nz >= s_d.d) return;
    s_x = nx;
    s_z = nz;
    if (s_painting) paint_here();
}

static void layer(int dy) {
    int const ny = s_y + dy;
    if (ny >= 0 && ny < s_d.h) s_y = ny;
}

static char const* brush_name(char c) {
    switch (c) {
        case '#':
            return "metal";
        case 'W':
            return "white panel";
        case '.':
            return "air";
        case '~':
            return "goo";
        case 'E':
            return "exit";
        case 'C':
            return "cube";
        case 'S':
            return "start";
        case 'G':
            return "glass";
        case 'F':
            return "fizzler";
        case 'J':
            return "faith plate";
        case 'T':
            return "plate target";
        case 'M':
            return "platform";
        case 'N':
            return "platform goes to";
        default:
            return chamber_is_door(c) ? "door" : chamber_is_button(c) ? "button for" : "?";
    }
}

static bool save(void) {
    static char text[24 * 1024];
    char        err[96];
    if (!draft_level(&s_d, &s_level, err, sizeof(err)) ||
        draft_save_text(&s_d, text, sizeof(text), err, sizeof(err)) < 0) {
        say("Not saved: %s", err);
        return false;
    }
    // The folder may not be there yet: /sd/portals, then its chambers.
    char parent[96];
    snprintf(parent, sizeof(parent), "%s", s_dir);
    char* slash = strrchr(parent, '/');
    if (slash != NULL && slash != parent) {
        *slash = '\0';
        mkdir(parent, 0755);
    }
    mkdir(s_dir, 0755);
    char path[sizeof(s_dir) + CHAMBER_ID_N + 8];
    snprintf(path, sizeof(path), "%s/%s.txt", s_dir, s_d.id);
    struct stat st;
    if (s_new && stat(path, &st) == 0) {
        say("Not saved: %s is already there", path);
        return false;
    }
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        say("Cannot write %s", path);
        return false;
    }
    size_t const n  = strlen(text);
    bool const   ok = fwrite(text, 1, n, f) == n;
    fclose(f);
    if (!ok) {
        say("Writing %s failed", path);
        return false;
    }
    s_new = false;  // from now on, this file is the draft's own
    chamber_reload_dir(s_dir);
    ESP_LOGI(TAG, "saved %s", path);
    say("Saved %s", path);
    return true;
}

static bool playtest(void) {
    char err[96];
    if (!draft_level(&s_d, &s_level, err, sizeof(err))) {
        say("Cannot play it: %s", err);
        return false;
    }
    s_want_test = true;
    return true;
}

// --- Keys -----------------------------------------------------------------

void editor_event(bsp_input_event_t const* ev) {
    if (!s_active) return;
    if (s_menu) {
        // The built-in keyboard sends Esc twice, as a scancode and as a
        // navigation key: the second must not close the menu the first opened.
        bool const esc =
            (ev->type == INPUT_EVENT_TYPE_SCANCODE && ev->args_scancode.scancode == BSP_INPUT_SCANCODE_ESC) ||
            (ev->type == INPUT_EVENT_TYPE_NAVIGATION && ev->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_ESC &&
             ev->args_navigation.state);
        if (esc && esp_timer_get_time() - s_menu_opened_us < 250000) return;
        if (ev->type == INPUT_EVENT_TYPE_SCANCODE) {
            uint16_t const sc = ev->args_scancode.scancode;
            if (sc & BSP_INPUT_SCANCODE_RELEASE_MODIFIER) return;
            switch (sc) {
                case BSP_INPUT_SCANCODE_ESC:
                    s_act |= A_BACK;
                    break;
                case BSP_INPUT_SCANCODE_ENTER:
                case BSP_INPUT_SCANCODE_SPACE:
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
        return;
    }
    if (ev->type != INPUT_EVENT_TYPE_SCANCODE) return;
    uint16_t const sc      = ev->args_scancode.scancode;
    bool const     release = (sc & BSP_INPUT_SCANCODE_RELEASE_MODIFIER) != 0;
    uint16_t const key     = sc & (uint16_t)~BSP_INPUT_SCANCODE_RELEASE_MODIFIER;
    if (key == BSP_INPUT_SCANCODE_SPACE || key == BSP_INPUT_SCANCODE_ENTER) {
        s_painting = !release;
        if (!release) paint_here();
        return;
    }
    if (release) return;
    switch (sc) {
        case BSP_INPUT_SCANCODE_ESC:
            s_menu_opened_us = esp_timer_get_time();
            s_menu           = true;
            s_menu_cursor    = 0;
            s_act            = 0;
            s_painting       = false;
            break;
        case BSP_INPUT_SCANCODE_1:
            s_brush = '#';
            break;
        case BSP_INPUT_SCANCODE_2:
            s_brush = 'W';
            break;
        case BSP_INPUT_SCANCODE_3:
            s_brush = '.';
            break;
        case BSP_INPUT_SCANCODE_4:
            s_brush = '~';
            break;
        case BSP_INPUT_SCANCODE_5:
            s_brush = 'E';
            break;
        case BSP_INPUT_SCANCODE_6:
            if (chamber_is_door(s_brush)) s_door = chamber_door_char((s_brush - 'a' + 1) % LV_MAX_DOORS);
            s_brush = s_door;
            break;
        case BSP_INPUT_SCANCODE_7:
            if (chamber_is_button(s_brush)) s_button = chamber_button_char((s_brush - '1' + 1) % LV_MAX_BUTTONS);
            s_brush = s_button;
            break;
        case BSP_INPUT_SCANCODE_8:
            s_brush = 'C';
            break;
        case BSP_INPUT_SCANCODE_9:
            s_brush = 'S';
            break;
        case BSP_INPUT_SCANCODE_0:
            s_brush = 'G';
            break;
        case BSP_INPUT_SCANCODE_MINUS:
            s_brush = 'F';
            break;
        case BSP_INPUT_SCANCODE_EQUAL:
            s_brush = 'J';
            break;
        case BSP_INPUT_SCANCODE_T:
            s_brush = 'T';
            break;
        case BSP_INPUT_SCANCODE_M:
            s_brush = 'M';
            break;
        case BSP_INPUT_SCANCODE_N:
            s_brush = 'N';
            break;
        case BSP_INPUT_SCANCODE_Q:
        case BSP_INPUT_SCANCODE_ESCAPED_GREY_PGDN:
            layer(-1);
            break;
        case BSP_INPUT_SCANCODE_E:
        case BSP_INPUT_SCANCODE_ESCAPED_GREY_PGUP:
            layer(+1);
            break;
        case BSP_INPUT_SCANCODE_BACKSPACE:
        case BSP_INPUT_SCANCODE_ESCAPED_GREY_DEL:
            draft_paint(&s_d, s_x, s_y, s_z, '.');
            break;
        case BSP_INPUT_SCANCODE_B:
            if (s_anchor_set) {
                box_fill();
                s_anchor_set = false;
            } else {
                s_ax         = s_x;
                s_az         = s_z;
                s_anchor_set = true;
            }
            break;
        case BSP_INPUT_SCANCODE_R:
            s_d.yaw += 1.5707963f;
            if (s_d.yaw > 3.2f) s_d.yaw -= 6.2831853f;
            break;
        case BSP_INPUT_SCANCODE_P:
            playtest();
            break;
        case BSP_INPUT_SCANCODE_F:
            save();
            break;
        default:
            break;
    }
}

static bool held(bsp_input_scancode_t sc) {
    bool state = false;
    return gl_input_read_scancode(sc, &state) == ESP_OK && state;
}

static bool nav_held(bsp_input_navigation_key_t key) {
    bool state = false;
    return gl_input_read_navigation_key(key, &state) == ESP_OK && state;
}

// --- The editor's menu ----------------------------------------------------

enum {
    M_BACK,
    M_TEST,
    M_SAVE,
    M_W,
    M_H,
    M_D,
    M_FACING,
    M_NEW,
    M_QUIT,
    M_ROWS
};

static char          s_val[4][16];
static se_menu_row_t s_rows[M_ROWS];

static void build_menu(se_menu_def_t* def) {
    static char const* const facing[4] = {"north", "east", "south", "west"};
    int                      f         = (int)lroundf(s_d.yaw / 1.5707963f);
    f                                  = ((f % 4) + 4) % 4;
    snprintf(s_val[0], sizeof(s_val[0]), "%d", s_d.w);
    snprintf(s_val[1], sizeof(s_val[1]), "%d", s_d.h);
    snprintf(s_val[2], sizeof(s_val[2]), "%d", s_d.d);
    snprintf(s_val[3], sizeof(s_val[3]), "%s", facing[f]);
    s_rows[M_BACK]   = (se_menu_row_t){.label = "Back to editing"};
    s_rows[M_TEST]   = (se_menu_row_t){.label = "Play-test"};
    s_rows[M_SAVE]   = (se_menu_row_t){.label = "Save to the SD card"};
    s_rows[M_W]      = (se_menu_row_t){.label = "Width", .kind = SE_MENU_VAL_TEXT, .value = s_val[0]};
    s_rows[M_H]      = (se_menu_row_t){.label = "Height", .kind = SE_MENU_VAL_TEXT, .value = s_val[1]};
    s_rows[M_D]      = (se_menu_row_t){.label = "Depth", .kind = SE_MENU_VAL_TEXT, .value = s_val[2]};
    s_rows[M_FACING] = (se_menu_row_t){.label = "Start facing", .kind = SE_MENU_VAL_TEXT, .value = s_val[3]};
    s_rows[M_NEW]    = (se_menu_row_t){.label = "New empty chamber"};
    s_rows[M_QUIT]   = (se_menu_row_t){.label = "Quit the editor"};
    *def             = (se_menu_def_t){
        .title     = "EDITOR",
        .subtitle  = s_d.id,
        .rows      = s_rows,
        .row_count = M_ROWS,
        .hint      = "Left / Right: change a size",
        .panel_w   = 0.70f,
        .panel_h   = 0.92f,
        .title_h   = 28.0f,
        .row_h     = 30.0f,
        .value_dx  = 270.0f,
    };
}

static void menu_frame(void) {
    uint32_t const act = s_act;
    s_act              = 0;
    se_menu_def_t def;
    build_menu(&def);
    se_menu_t m = {.def = &def, .cursor = s_menu_cursor};
    if (act & A_UP) se_menu_input(&m, SE_MENU_ACT_UP);
    if (act & A_DOWN) se_menu_input(&m, SE_MENU_ACT_DOWN);
    se_menu_result_t r = SE_MENU_RESULT_NONE;
    if (act & A_OK) r = se_menu_input(&m, SE_MENU_ACT_ACTIVATE);
    if (act & A_BACK) r = se_menu_input(&m, SE_MENU_ACT_BACK);
    if (m.cursor != s_menu_cursor || r == SE_MENU_RESULT_ACTIVATED) sound_play(SND_MENU);
    s_menu_cursor  = m.cursor;
    int const step = (act & A_RIGHT) ? 1 : (act & A_LEFT) ? -1 : 0;

    if (r == SE_MENU_RESULT_BACK || (r == SE_MENU_RESULT_ACTIVATED && s_menu_cursor == M_BACK)) {
        s_menu = false;
    } else if (r == SE_MENU_RESULT_ACTIVATED && s_menu_cursor == M_TEST) {
        if (playtest()) s_menu = false;
    } else if (r == SE_MENU_RESULT_ACTIVATED && s_menu_cursor == M_SAVE) {
        save();
        s_menu = false;
    } else if (step != 0 && s_menu_cursor >= M_W && s_menu_cursor <= M_D) {
        draft_resize(&s_d, s_d.w + (s_menu_cursor == M_W ? step : 0), s_d.h + (s_menu_cursor == M_H ? step : 0),
                     s_d.d + (s_menu_cursor == M_D ? step : 0));
        if (s_x >= s_d.w) s_x = s_d.w - 1;
        if (s_z >= s_d.d) s_z = s_d.d - 1;
        if (s_y >= s_d.h) s_y = s_d.h - 1;
    } else if (s_menu_cursor == M_FACING && (step != 0 || r == SE_MENU_RESULT_ACTIVATED)) {
        // Whole quarter turns, snapped: turned often, it must still say
        // "east", not drift off it.
        float q = roundf(s_d.yaw / 1.5707963f) + (step < 0 ? -1.0f : 1.0f);
        if (q > 2.0f) q -= 4.0f;
        if (q < -1.0f) q += 4.0f;
        s_d.yaw = q * 1.5707963f;
    } else if (r == SE_MENU_RESULT_ACTIVATED && s_menu_cursor == M_NEW) {
        char id[CHAMBER_ID_N];
        fresh_id(id, sizeof(id), NULL);
        draft_new(&s_d, id, 12, 6, 12);
        s_new = true;
        s_x = s_d.w / 2, s_z = s_d.d / 2, s_y = 1;
        s_anchor_set = s_painting = false;
        say("New chamber %s", id);
        s_menu = false;
    } else if (r == SE_MENU_RESULT_ACTIVATED && s_menu_cursor == M_QUIT) {
        s_menu      = false;
        s_want_quit = true;
    }
}

editor_cmd_t editor_update(float dt) {
    if (!s_active) return EDITOR_CMD_NONE;
    if (s_msg_t > 0.0f) s_msg_t -= dt;
    if (s_menu) {
        menu_frame();
    } else {
        // The cursor: arrows (either kind) or WASD, repeating while held.
        bool const dir[4] = {
            held(BSP_INPUT_SCANCODE_ESCAPED_GREY_UP) || nav_held(BSP_INPUT_NAVIGATION_KEY_UP) ||
                held(BSP_INPUT_SCANCODE_W),
            held(BSP_INPUT_SCANCODE_ESCAPED_GREY_DOWN) || nav_held(BSP_INPUT_NAVIGATION_KEY_DOWN) ||
                held(BSP_INPUT_SCANCODE_S),
            held(BSP_INPUT_SCANCODE_ESCAPED_GREY_LEFT) || nav_held(BSP_INPUT_NAVIGATION_KEY_LEFT) ||
                held(BSP_INPUT_SCANCODE_A),
            held(BSP_INPUT_SCANCODE_ESCAPED_GREY_RIGHT) || nav_held(BSP_INPUT_NAVIGATION_KEY_RIGHT) ||
                held(BSP_INPUT_SCANCODE_D),
        };
        // A press moves once; held, it moves again after REPEAT_FIRST and
        // then every REPEAT_NEXT.
        int const dx[4] = {0, 0, -1, 1}, dz[4] = {1, -1, 0, 0};  // up the screen is the far side
        for (int i = 0; i < 4; i++) {
            if (!dir[i]) {
                s_held_t[i] = 0.0f;
                continue;
            }
            if (s_held_t[i] == 0.0f) {
                move(dx[i], dz[i]);
                s_next_t[i] = REPEAT_FIRST;
            }
            s_held_t[i] += dt;
            while (s_held_t[i] >= s_next_t[i]) {
                move(dx[i], dz[i]);
                s_next_t[i] += REPEAT_NEXT;
            }
        }
    }
    if (s_want_test) {
        s_want_test = false;
        s_active    = false;
        return EDITOR_CMD_PLAYTEST;
    }
    if (s_want_quit) {
        s_want_quit = false;
        s_active    = false;
        return EDITOR_CMD_QUIT;
    }
    return EDITOR_CMD_NONE;
}

// --- Drawing --------------------------------------------------------------

static uint32_t cell_colour(char c, bool floor_below) {
    switch (c) {
        case '#':
            return 0xFF3A3D44u;
        case 'W':
            return 0xFFD0D0C8u;
        case '~':
            return 0xFF8A7020u;
        case 'E':
            return 0xFF30C060u;
        case 'C':
            return 0xFF9AA0A8u;
        case 'S':
            return floor_below ? 0xFF2C2F36u : 0xFF15161Au;
        case 'G':
            return 0xFF8EC8E0u;
        case 'F':
            return 0xFF3070D0u;
        case 'J':
            return 0xFFE08020u;
        case 'T':
            return 0xFF6A3A10u;
        case 'M':
            return 0xFF5A6070u;
        case 'N':
            return 0xFF2C5C9Cu;
        case '.':
        case ' ':
            return floor_below ? 0xFF2C2F36u : 0xFF15161Au;
        default:
            if (chamber_is_door(c)) return 0xFFE08030u;
            if (chamber_is_button(c)) return 0xFFC03020u;
            return 0xFFFF00FFu;
    }
}

void editor_draw(pax_buf_t* fb) {
    pax_background(fb, 0xFF0C0D10u);
    int const gw = 540, gh = 440;
    int       cs = gw / s_d.w < gh / s_d.d ? gw / s_d.w : gh / s_d.d;
    if (cs > 32) cs = 32;
    int const x0 = 12 + (gw - cs * s_d.w) / 2, y0 = 24 + (gh - cs * s_d.d) / 2;

    for (int z = 0; z < s_d.d; z++) {
        for (int x = 0; x < s_d.w; x++) {
            char const  c     = draft_get(&s_d, x, s_y, z);
            char const  below = draft_get(&s_d, x, s_y - 1, z);
            bool const  floor = s_y > 0 && below != '.' && below != ' ' && below != 'S' && below != 'C' &&
                                !chamber_is_button(below) && below != 'T' && below != 'M' && below != 'N';
            float const px = (float)(x0 + x * cs), py = (float)(y0 + (s_d.d - 1 - z) * cs);
            pax_simple_rect(fb, cell_colour(c, floor), px, py, (float)cs - 1, (float)cs - 1);
            char label[2] = {0};
            if (chamber_is_door(c) || chamber_is_button(c)) label[0] = c;
            if (c == 'C') pax_simple_circle(fb, 0xFF6EB4E6u, px + cs * 0.5f, py + cs * 0.5f, cs * 0.18f);
            if (c == 'S') {
                // The start, pointing the way it faces.
                float const s = sinf(s_d.yaw), co = cosf(s_d.yaw), r = cs * 0.38f;
                float const cx = px + cs * 0.5f, cy = py + cs * 0.5f;
                pax_draw_tri(fb, 0xFF40D0F0u, cx + s * r, cy - co * r, cx - co * r * 0.6f - s * r * 0.5f,
                             cy - s * r * 0.6f + co * r * 0.5f, cx + co * r * 0.6f - s * r * 0.5f,
                             cy + s * r * 0.6f + co * r * 0.5f);
            }
            if (label[0] && cs >= 12)
                pax_draw_text(fb, 0xFF000000u, pax_font_sky_mono, (float)cs * 0.7f, px + cs * 0.25f, py + cs * 0.1f,
                              label);
        }
    }
    // The box being marked, and the cursor.
    if (s_anchor_set) {
        int const ax0 = s_ax < s_x ? s_ax : s_x, ax1 = s_ax < s_x ? s_x : s_ax;
        int const az0 = s_az < s_z ? s_az : s_z, az1 = s_az < s_z ? s_z : s_az;
        pax_outline_rect(fb, 0xFF40D0F0u, (float)(x0 + ax0 * cs), (float)(y0 + (s_d.d - 1 - az1) * cs),
                         (float)((ax1 - ax0 + 1) * cs - 1), (float)((az1 - az0 + 1) * cs - 1));
    }
    float const cx = (float)(x0 + s_x * cs), cy = (float)(y0 + (s_d.d - 1 - s_z) * cs);
    pax_outline_rect(fb, 0xFFFFFF40u, cx - 1, cy - 1, (float)cs + 1, (float)cs + 1);
    pax_outline_rect(fb, 0xFFFFFF40u, cx - 2, cy - 2, (float)cs + 3, (float)cs + 3);

    // The panel on the right.
    float const tx = 570;
    char        line[96];
    pax_draw_text(fb, 0xFFFFFF6Bu, pax_font_sky_mono, 18, tx, 8, "EDITOR");
    pax_draw_text(fb, 0xFFFFFFFFu, pax_font_sky_mono, 12, tx, 32, s_d.id);
    snprintf(line, sizeof(line), "layer %d of %d  (Q/E)", s_y, s_d.h - 1);
    pax_draw_text(fb, 0xFFFFFFFFu, pax_font_sky_mono, 12, tx, 52, line);
    snprintf(line, sizeof(line), "x %d  z %d   %dx%dx%d", s_x, s_z, s_d.w, s_d.h, s_d.d);
    pax_draw_text(fb, 0xFFA0A0A8u, pax_font_sky_mono, 12, tx, 68, line);

    static struct {
        char key;
        char ch;
    } const palette[] = {{'1', '#'}, {'2', 'W'}, {'3', '.'}, {'4', '~'}, {'5', 'E'}, {'6', 'a'}, {'7', '1'}, {'8', 'C'},
                         {'9', 'S'}, {'0', 'G'}, {'-', 'F'}, {'=', 'J'}, {'T', 'T'}, {'M', 'M'}, {'N', 'N'}};
    int const n_palette = (int)(sizeof(palette) / sizeof(palette[0]));
    for (int i = 0; i < n_palette; i++) {
        char ch = palette[i].ch;
        if (ch == 'a') ch = s_door;
        if (ch == '1') ch = s_button;
        float const y   = 86.0f + (float)i * 16.0f;
        bool const  sel = s_brush == ch;
        pax_simple_rect(fb, cell_colour(ch, true), tx, y, 14, 14);
        snprintf(line, sizeof(line), "%c %s%s%c", palette[i].key, brush_name(ch),
                 chamber_is_door(ch) || chamber_is_button(ch) ? " " : "",
                 chamber_is_door(ch)     ? ch
                 : chamber_is_button(ch) ? chamber_door_char(ch - '1')
                                         : ' ');
        pax_draw_text(fb, sel ? 0xFFFFFF6Bu : 0xFFFFFFFFu, pax_font_sky_mono, 12, tx + 20, y + 1, line);
    }
    static char const* const help[] = {
        "arrows  move",       "Space   paint (hold)", "B       box, twice", "Bksp    erase",
        "R       turn start", "P       play-test",    "F       save",       "Esc     menu",
    };
    for (int i = 0; i < 8; i++)
        pax_draw_text(fb, 0xFFA0A0A8u, pax_font_sky_mono, 12, tx, 334.0f + (float)i * 16.0f, help[i]);
    if (s_msg_t > 0.0f) pax_draw_text(fb, 0xFFFFFFFFu, pax_font_sky_mono, 12, 12, 4, s_msg);

    if (s_menu) {
        se_menu_def_t def;
        build_menu(&def);
        se_menu_t const m = {.def = &def, .cursor = s_menu_cursor};
        se_menu_draw(&m, fb);
    }
}
