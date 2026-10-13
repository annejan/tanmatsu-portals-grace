// The title screen's drawing for tests/host_title.c (tools/title_film.py):
// main.c's on_render on the title, on an RGB565 framebuffer laid out as
// the badge's. Its own translation unit, as tests/movie_hud.c is -- whose
// framebuffer, PAX stand-ins, hud.c, input.c and the engine's text
// (rendertext.c) it takes in whole -- with the game's own menu
// (main/menu.c) on the engine's own list menu (synthengine3D's se_ui.c).
// The game's settings (main/settings.c, built beside it) read from an NVS
// stood in for here, which holds only the chamber Continue goes back to:
// a badge left with every setting as it ships.
//
// A frame: the rasterizer's picture (at a quarter of the pixels, doubled,
// as the PPA blits the half-size layer, or whole) packed to 565,
// hud_fade'd by attract_lit, then hud_draw_title and menu_draw over it.

#include "movie_hud.c"

// The engine's, which the real synthengine3d.h would bring (tests/shims
// has only the bindings).
#include "se_hw.h"
#include "se_ui.h"

#include "../main/menu.c"
#include "../synthengine3D/src/se_ui.c"

#include "nvs.h"
#include "title_ui.h"

// --- What PAX and the engine give the menu --------------------------------

int pax_buf_get_width(pax_buf_t const* buf) {
    (void)buf;
    return DISPLAY_LOG_W;  // the logical width, as a rotated buffer reports it
}
int pax_buf_get_height(pax_buf_t const* buf) {
    (void)buf;
    return DISPLAY_LOG_H;
}

// Settings' rows only; the title never shows them.
uint8_t se_hw_get_volume(void) {
    return 50;
}
void se_hw_set_volume(uint8_t p) {
    (void)p;
}
uint8_t se_hw_get_display_brightness(void) {
    return 100;
}
void se_hw_set_display_brightness(uint8_t p) {
    (void)p;
}
uint8_t se_hw_get_keyboard_brightness(void) {
    return 100;
}
void se_hw_set_keyboard_brightness(uint8_t p) {
    (void)p;
}
uint16_t se_ui_capture_key(char const* prompt) {
    (void)prompt;
    return 0;
}

// --- NVS: the chamber Continue goes back to, and nothing else --------------

static char s_nvs_chamber[CHAMBER_ID_N];

esp_err_t nvs_open(char const* ns, nvs_open_mode_t mode, nvs_handle_t* h) {
    (void)ns;
    *h = 1;
    return mode == NVS_READONLY ? ESP_OK : ESP_FAIL;  // nothing written back
}
esp_err_t nvs_get_u8(nvs_handle_t h, char const* key, uint8_t* v) {
    (void)h, (void)key, (void)v;
    return ESP_ERR_NVS_NOT_FOUND;  // every setting as it ships
}
esp_err_t nvs_get_str(nvs_handle_t h, char const* key, char* out, size_t* n) {
    (void)h;
    if (strcmp(key, "chamber") != 0 || s_nvs_chamber[0] == '\0') return ESP_ERR_NVS_NOT_FOUND;
    snprintf(out, *n, "%s", s_nvs_chamber);
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h, char const* key, uint8_t v) {
    (void)h, (void)key, (void)v;
    return ESP_FAIL;
}
esp_err_t nvs_set_str(nvs_handle_t h, char const* key, char const* v) {
    (void)h, (void)key, (void)v;
    return ESP_FAIL;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h;
    return ESP_FAIL;
}
void nvs_close(nvs_handle_t h) {
    (void)h;
}

// --- For host_title.c ------------------------------------------------------

void title_ui_init(char const* continue_id) {
    snprintf(s_nvs_chamber, sizeof(s_nvs_chamber), "%s", continue_id != NULL ? continue_id : "");
    settings_load();  // on_init's
    input_init();
}

void title_ui_open(void) {
    // main.c's to_title: Continue to the chamber the settings name (a desk
    // story's "desk:<pack>" too).
    int story = -1;
    if (strncmp(settings_chamber(), "desk:", 5) == 0)
        for (int i = 0; i < pack_count(); i++)
            if (pack_get(i)->desk && strcmp(pack_get(i)->id, settings_chamber() + 5) == 0) story = i;
    if (story >= 0)
        menu_title_story(story);
    else
        menu_title(chamber_find(settings_chamber()));
}

bool title_ui_on_title(void) {
    return menu_on_title();
}

bool title_ui_menu_music(void) {
    return settings_menu_music();
}

bool title_ui_half(void) {
    return settings_half_res();
}

int title_ui_portal_depth(void) {
    return settings_portal_depth();
}

void title_ui_key(char key) {
    // A key on the built-in keyboard, as its navigation event (it sends
    // a scancode too; menu.c counts the two once): a cursor key, Enter,
    // or Esc.
    bsp_input_event_t ev = {.type = INPUT_EVENT_TYPE_NAVIGATION};
    ev.args_navigation.key   = key == 'u'   ? BSP_INPUT_NAVIGATION_KEY_UP
                               : key == 'd' ? BSP_INPUT_NAVIGATION_KEY_DOWN
                               : key == 'o' ? BSP_INPUT_NAVIGATION_KEY_RETURN
                                            : BSP_INPUT_NAVIGATION_KEY_ESC;
    ev.args_navigation.state = true;
    menu_event(&ev);
}

void title_ui_update(void) {
    (void)menu_update();  // the cursor moves; nothing is chosen
}

void title_ui_frame(uint32_t* px, bool quarter, float lit, game_t const* g) {
    // The 3D picture, as the PPA leaves it in the framebuffer.
    for (int y = 0; y < DISPLAY_LOG_H; y++)
        for (int x = 0; x < DISPLAY_LOG_W; x++)
            s_fb565[fb_index(x, y)] =
                pack565(px[quarter ? (y / 2) * DISPLAY_LOG_W + x / 2 : y * DISPLAY_LOG_W + x]);
    // main.c's on_render, the title's path: faded by attract_lit (on the
    // half-size layer there; pixel for pixel the same), then the title's
    // words and the menu over it.
    if (lit < 1.0f) hud_fade(&s_fb, lit);
    hud_draw_title(&s_fb, g, menu_title_shown());
    menu_draw(&s_fb);
    fb_out(px);
}
