#include "settings.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chamber.h"
#include "esp_log.h"
#include "nvs.h"
#include "render.h"

static bool s_gyro  = false;
static bool s_half  = true;
static int  s_depth = 2;
static bool s_music = true;
static bool s_fx    = true;
static bool s_leds  = true;
static bool s_voice = true;
static bool s_ftime = false;
static bool s_ghost = true;
static bool s_lifts = true;
static char s_chamber[CHAMBER_ID_N];

static uint8_t get(nvs_handle_t h, char const* key, uint8_t fallback) {
    uint8_t v = fallback;
    return nvs_get_u8(h, key, &v) == ESP_OK ? v : fallback;
}

static void put(char const* key, uint8_t v) {
    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW("settings", "cannot save %s", key);
        return;
    }
    nvs_set_u8(h, key, v);
    nvs_commit(h);
    nvs_close(h);
}

void settings_load(void) {
    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        s_gyro   = get(h, "gyro", s_gyro) != 0;
        s_half   = get(h, "half", s_half) != 0;
        s_depth  = get(h, "depth", (uint8_t)s_depth);
        s_music  = get(h, "music", s_music) != 0;
        s_fx     = get(h, "sfx", s_fx) != 0;
        s_leds   = get(h, "leds", s_leds) != 0;
        s_voice  = get(h, "voice", s_voice) != 0;
        s_ftime  = get(h, "ftime", s_ftime) != 0;
        s_ghost  = get(h, "ghost", s_ghost) != 0;
        s_lifts  = get(h, "lifts", s_lifts) != 0;
        size_t n = sizeof(s_chamber);
        if (nvs_get_str(h, "chamber", s_chamber, &n) != ESP_OK) s_chamber[0] = '\0';
        nvs_close(h);
    }
    render_set_portal_depth(s_depth);
    s_depth = render_portal_depth();  // clamped
}

bool settings_gyro(void) {
    return s_gyro;
}

void settings_set_gyro(bool on) {
    s_gyro = on;
    put("gyro", on);
}

bool settings_music(void) {
    return s_music;
}

void settings_set_music(bool on) {
    s_music = on;
    put("music", on);
}

bool settings_effects(void) {
    return s_fx;
}

void settings_set_effects(bool on) {
    s_fx = on;
    put("sfx", on);
}

bool settings_voice(void) {
    return s_voice;
}

void settings_set_voice(bool on) {
    s_voice = on;
    put("voice", on);
}

bool settings_lifts(void) {
    return s_lifts;
}

void settings_set_lifts(bool on) {
    s_lifts = on;
    put("lifts", on);
}

bool settings_ghosts(void) {
    return s_ghost;
}

void settings_set_ghosts(bool on) {
    s_ghost = on;
    put("ghost", on);
}

bool settings_frame_times(void) {
    return s_ftime;
}

void settings_set_frame_times(bool on) {
    s_ftime = on;
    put("ftime", on);
}

bool settings_leds(void) {
    return s_leds;
}

void settings_set_leds(bool on) {
    s_leds = on;
    put("leds", on);
}

bool settings_half_res(void) {
    return s_half;
}

void settings_set_half_res(bool on) {
    s_half = on;
    put("half", on);
}

int settings_portal_depth(void) {
    return s_depth;
}

void settings_set_portal_depth(int depth) {
    render_set_portal_depth(depth);
    s_depth = render_portal_depth();
    put("depth", (uint8_t)s_depth);
}

char const* settings_chamber(void) {
    return s_chamber;
}

void settings_set_chamber(char const* id) {
    if (strcmp(id, s_chamber) == 0) return;
    // Kept only once it is written: a failed write is tried again next time.
    nvs_handle_t h;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW("settings", "cannot save chamber %s", id);
        return;
    }
    if (nvs_set_str(h, "chamber", id) == ESP_OK && nvs_commit(h) == ESP_OK)
        snprintf(s_chamber, sizeof(s_chamber), "%s", id);
    else
        ESP_LOGW("settings", "cannot save chamber %s", id);
    nvs_close(h);
}
