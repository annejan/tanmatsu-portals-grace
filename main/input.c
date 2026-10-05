#include "input.h"
#include <math.h>
#include <stdio.h>
#include "bsp/input.h"
#include "esp_log.h"
#include "gl_input.h"
#include "graceloader_imu.h"
#include "settings.h"
#include "synthengine3d.h"

#define LOOK_SPEED 2.0f  // radians a second on the look keys

static se_binding_def_t const s_defs[ACT_COUNT] = {
    {ACT_FORWARD, "Forward", "k_fwd", BSP_INPUT_SCANCODE_W},
    {ACT_BACK, "Back", "k_back", BSP_INPUT_SCANCODE_S},
    {ACT_LEFT, "Left", "k_left", BSP_INPUT_SCANCODE_A},
    {ACT_RIGHT, "Right", "k_right", BSP_INPUT_SCANCODE_D},
    {ACT_JUMP, "Jump", "k_jump", BSP_INPUT_SCANCODE_SPACE},
    {ACT_BLUE, "Blue portal", "k_blue", BSP_INPUT_SCANCODE_Q},
    {ACT_ORANGE, "Orange portal", "k_orange", BSP_INPUT_SCANCODE_E},
    {ACT_USE, "Pick up / put down", "k_use", BSP_INPUT_SCANCODE_F},
    {ACT_LOOK_UP, "Look up", "k_lup", BSP_INPUT_SCANCODE_ESCAPED_GREY_UP},
    {ACT_LOOK_DOWN, "Look down", "k_ldown", BSP_INPUT_SCANCODE_ESCAPED_GREY_DOWN},
    {ACT_LOOK_LEFT, "Look left", "k_lleft", BSP_INPUT_SCANCODE_ESCAPED_GREY_LEFT},
    {ACT_LOOK_RIGHT, "Look right", "k_lright", BSP_INPUT_SCANCODE_ESCAPED_GREY_RIGHT},
    {ACT_RESTART, "Restart chamber", "k_restart", BSP_INPUT_SCANCODE_R},
    {ACT_GYRO, "Gyroscope on/off", "k_gyro", BSP_INPUT_SCANCODE_G},
};

void input_init(void) {
    static se_bindings_config_t const cfg = {SETTINGS_NVS_NAMESPACE, s_defs, ACT_COUNT};
    se_bindings_init(&cfg);
}

char const* input_action_label(action_t a) {
    return a < ACT_COUNT ? s_defs[a].label : "?";
}

uint16_t input_key(action_t a) {
    return se_bindings_get(a);
}

void input_bind(action_t a, uint16_t sc) {
    // No two actions share a key: whatever had it takes this one's old key.
    uint16_t const old = se_bindings_get(a);
    for (int i = 0; i < ACT_COUNT; i++)
        if (i != (int)a && se_bindings_get(i) == sc) se_bindings_set(i, old);
    se_bindings_set(a, sc);
}

void input_reset_defaults(void) {
    for (int i = 0; i < ACT_COUNT; i++) se_bindings_set(i, s_defs[i].default_sc);
}

// --- Held keys ------------------------------------------------------------

static bool sc_held(uint16_t sc) {
    bool state = false;
    return sc != 0 && gl_input_read_scancode((bsp_input_scancode_t)sc, &state) == ESP_OK && state;
}

static bool nav_held(bsp_input_navigation_key_t key) {
    bool state = false;
    return gl_input_read_navigation_key(key, &state) == ESP_OK && state;
}

// The cursor keys come as escaped "grey" scancodes from a PC keyboard
// and as navigation keys from the built-in one; a binding to one of
// them answers to both.
static bool held(action_t a) {
    uint16_t const sc = se_bindings_get(a);
    if (sc_held(sc)) return true;
    switch (sc) {
        case BSP_INPUT_SCANCODE_ESCAPED_GREY_UP:
            return nav_held(BSP_INPUT_NAVIGATION_KEY_UP);
        case BSP_INPUT_SCANCODE_ESCAPED_GREY_DOWN:
            return nav_held(BSP_INPUT_NAVIGATION_KEY_DOWN);
        case BSP_INPUT_SCANCODE_ESCAPED_GREY_LEFT:
            return nav_held(BSP_INPUT_NAVIGATION_KEY_LEFT);
        case BSP_INPUT_SCANCODE_ESCAPED_GREY_RIGHT:
            return nav_held(BSP_INPUT_NAVIGATION_KEY_RIGHT);
        default:
            return false;
    }
}

static float axis(action_t neg, action_t pos) {
    return (held(pos) ? 1.0f : 0.0f) - (held(neg) ? 1.0f : 0.0f);
}

// --- Gyroscope (after SynthMiner's game/input.c) --------------------------
//
// The RATE gyroscope: the turn rate is added up frame by frame and handed
// to the look like the look keys' delta -- turn the badge 30 degrees and
// the view turns 30 degrees. Held upright, device X runs top to bottom
// (turning left / right is about it) and Y runs across (tipping the
// screen is about it). The signs are the first thing to flip if the
// view turns the wrong way.
#define GYRO_YAW_SIGN   (+1.0f)
#define GYRO_PITCH_SIGN (+1.0f)
#define GYRO_GAIN       1.0f
// A resting gyroscope does not read zero. Readings this slow are taken
// as the sensor's own offset and tracked, not turned into a slow spin.
#define GYRO_REST_DPS   3.0f
#define GYRO_BIAS_RATE  0.02f
#define GYRO_MAX_STEP   0.6f

static bool  s_gyro_started;
static float s_bias_x, s_bias_y;

static float gyro_axis(float rate, float* bias) {
    float const r = rate - *bias;
    if (fabsf(r) < GYRO_REST_DPS) {
        *bias += r * GYRO_BIAS_RATE;
        return 0.0f;
    }
    return r;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void gyro(float dt, float* dyaw, float* dpitch) {
    if (!s_gyro_started) {
        esp_err_t const e = bsp_orientation_enable_gyroscope();
        if (e != ESP_OK) {
            ESP_LOGW("input", "gyroscope did not start: %d", e);
            return;
        }
        s_gyro_started = true;
    }
    bool  ready = false;
    float gx = 0.0f, gy = 0.0f;
    if (bsp_orientation_get(&ready, NULL, &gx, &gy, NULL, NULL, NULL, NULL) != ESP_OK || !ready) return;
    float const k  = GYRO_GAIN * dt * (3.14159265f / 180.0f);
    *dyaw         += clampf(GYRO_YAW_SIGN * gyro_axis(gx, &s_bias_x) * k, -GYRO_MAX_STEP, GYRO_MAX_STEP);
    *dpitch       += clampf(GYRO_PITCH_SIGN * gyro_axis(gy, &s_bias_y) * k, -GYRO_MAX_STEP, GYRO_MAX_STEP);
}

// --- Polling ---------------------------------------------------------

static bool s_last[ACT_COUNT];
static bool s_latch[ACT_COUNT];  // pressed since the last poll, seen in an event

// A press is an edge in the held state between two polls -- or a key
// event in between. At 15 fps a frame is 66 ms, and a quick tap went down
// and up again between two polls without either seeing it.
static bool pressed(action_t a) {
    bool const now = held(a);
    bool const was = s_last[a];
    s_last[a]      = now;
    bool const hit = (now && !was) || s_latch[a];
    s_latch[a]     = false;
    return hit;
}

void input_event(bsp_input_event_t const* ev) {
    uint16_t sc = 0;
    if (ev->type == INPUT_EVENT_TYPE_SCANCODE) {
        sc = ev->args_scancode.scancode;
        if (sc & BSP_INPUT_SCANCODE_RELEASE_MODIFIER) return;
    } else if (ev->type == INPUT_EVENT_TYPE_NAVIGATION && ev->args_navigation.state) {
        // The built-in keyboard's cursor keys, as the scancodes a binding holds.
        switch (ev->args_navigation.key) {
            case BSP_INPUT_NAVIGATION_KEY_UP:
                sc = BSP_INPUT_SCANCODE_ESCAPED_GREY_UP;
                break;
            case BSP_INPUT_NAVIGATION_KEY_DOWN:
                sc = BSP_INPUT_SCANCODE_ESCAPED_GREY_DOWN;
                break;
            case BSP_INPUT_NAVIGATION_KEY_LEFT:
                sc = BSP_INPUT_SCANCODE_ESCAPED_GREY_LEFT;
                break;
            case BSP_INPUT_NAVIGATION_KEY_RIGHT:
                sc = BSP_INPUT_SCANCODE_ESCAPED_GREY_RIGHT;
                break;
            default:
                return;
        }
    } else {
        return;
    }
    for (int i = 0; i < ACT_COUNT; i++)
        if (se_bindings_get(i) == sc) s_latch[i] = true;
}

void input_resync(void) {
    for (int i = 0; i < ACT_COUNT; i++) {
        s_last[i]  = held((action_t)i);
        s_latch[i] = false;
    }
}

void input_poll(input_frame_t* out, float dt, bool gyro_on) {
    *out = (input_frame_t){0};

    out->fire[0] = pressed(ACT_BLUE);
    out->fire[1] = pressed(ACT_ORANGE);
    out->use     = pressed(ACT_USE);
    out->restart = pressed(ACT_RESTART);
    out->gyro    = pressed(ACT_GYRO);

    out->fwd    = axis(ACT_BACK, ACT_FORWARD);
    out->strafe = axis(ACT_LEFT, ACT_RIGHT);
    out->jump   = held(ACT_JUMP);

    out->dyaw   = axis(ACT_LOOK_LEFT, ACT_LOOK_RIGHT) * LOOK_SPEED * dt;
    out->dpitch = axis(ACT_LOOK_UP, ACT_LOOK_DOWN) * LOOK_SPEED * dt;  // positive pitch looks down
    if (gyro_on) gyro(dt, &out->dyaw, &out->dpitch);
}

// --- Key names (after SynthMiner's input_key_name) ------------------------

char const* input_key_name(uint16_t sc, char* buf, int cap) {
    static struct {
        uint16_t    sc;
        char const* name;
    } const NAMES[] = {
        {BSP_INPUT_SCANCODE_ESC, "Esc"},
        {BSP_INPUT_SCANCODE_SPACE, "Space"},
        {BSP_INPUT_SCANCODE_ENTER, "Enter"},
        {BSP_INPUT_SCANCODE_BACKSPACE, "Backspace"},
        {BSP_INPUT_SCANCODE_TAB, "Tab"},
        {BSP_INPUT_SCANCODE_CAPSLOCK, "Caps Lock"},
        {BSP_INPUT_SCANCODE_LEFTSHIFT, "Left Shift"},
        {BSP_INPUT_SCANCODE_RIGHTSHIFT, "Right Shift"},
        {BSP_INPUT_SCANCODE_LEFTCTRL, "Ctrl"},
        {BSP_INPUT_SCANCODE_LEFTALT, "Alt"},
        {BSP_INPUT_SCANCODE_FN, "Fn"},
        {BSP_INPUT_SCANCODE_MINUS, "-"},
        {BSP_INPUT_SCANCODE_EQUAL, "="},
        {BSP_INPUT_SCANCODE_LEFTBRACE, "["},
        {BSP_INPUT_SCANCODE_RIGHTBRACE, "]"},
        {BSP_INPUT_SCANCODE_SEMICOLON, ";"},
        {BSP_INPUT_SCANCODE_APOSTROPHE, "'"},
        {BSP_INPUT_SCANCODE_GRAVE, "`"},
        {BSP_INPUT_SCANCODE_BACKSLASH, "\\"},
        {BSP_INPUT_SCANCODE_COMMA, ","},
        {BSP_INPUT_SCANCODE_DOT, "."},
        {BSP_INPUT_SCANCODE_SLASH, "/"},
        {BSP_INPUT_SCANCODE_ESCAPED_GREY_UP, "Up"},
        {BSP_INPUT_SCANCODE_ESCAPED_GREY_DOWN, "Down"},
        {BSP_INPUT_SCANCODE_ESCAPED_GREY_LEFT, "Left"},
        {BSP_INPUT_SCANCODE_ESCAPED_GREY_RIGHT, "Right"},
        {BSP_INPUT_SCANCODE_ESCAPED_GREY_HOME, "Home"},
        {BSP_INPUT_SCANCODE_ESCAPED_GREY_END, "End"},
        {BSP_INPUT_SCANCODE_ESCAPED_GREY_PGUP, "Page Up"},
        {BSP_INPUT_SCANCODE_ESCAPED_GREY_PGDN, "Page Down"},
        {BSP_INPUT_SCANCODE_ESCAPED_GREY_INSERT, "Insert"},
        {BSP_INPUT_SCANCODE_ESCAPED_GREY_DEL, "Delete"},
        {BSP_INPUT_SCANCODE_ESCAPED_RCTRL, "Right Ctrl"},
        {BSP_INPUT_SCANCODE_ESCAPED_RALT, "Right Alt"},
        {BSP_INPUT_SCANCODE_ESCAPED_KPENTER, "Keypad Enter"},
    };
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
        if (NAMES[i].sc == sc) {
            snprintf(buf, (size_t)cap, "%s", NAMES[i].name);
            return buf;
        }
    }
    // The rows the scancode set lays out in order.
    static char const ROW1[] = "1234567890";
    static char const ROWQ[] = "QWERTYUIOP";
    static char const ROWA[] = "ASDFGHJKL";
    static char const ROWZ[] = "ZXCVBNM";
    if (sc >= BSP_INPUT_SCANCODE_1 && sc <= BSP_INPUT_SCANCODE_0) {
        snprintf(buf, (size_t)cap, "%c", ROW1[sc - BSP_INPUT_SCANCODE_1]);
    } else if (sc >= BSP_INPUT_SCANCODE_Q && sc <= BSP_INPUT_SCANCODE_P) {
        snprintf(buf, (size_t)cap, "%c", ROWQ[sc - BSP_INPUT_SCANCODE_Q]);
    } else if (sc >= BSP_INPUT_SCANCODE_A && sc <= BSP_INPUT_SCANCODE_L) {
        snprintf(buf, (size_t)cap, "%c", ROWA[sc - BSP_INPUT_SCANCODE_A]);
    } else if (sc >= BSP_INPUT_SCANCODE_Z && sc <= BSP_INPUT_SCANCODE_M) {
        snprintf(buf, (size_t)cap, "%c", ROWZ[sc - BSP_INPUT_SCANCODE_Z]);
    } else if (sc >= BSP_INPUT_SCANCODE_F1 && sc <= BSP_INPUT_SCANCODE_F10) {
        snprintf(buf, (size_t)cap, "F%d", sc - BSP_INPUT_SCANCODE_F1 + 1);
    } else if (sc == BSP_INPUT_SCANCODE_F11 || sc == BSP_INPUT_SCANCODE_F12) {
        snprintf(buf, (size_t)cap, "F%d", sc == BSP_INPUT_SCANCODE_F11 ? 11 : 12);
    } else if (sc == 0) {
        snprintf(buf, (size_t)cap, "(none)");
    } else {
        snprintf(buf, (size_t)cap, "Key %04X", (unsigned)sc);
    }
    return buf;
}
