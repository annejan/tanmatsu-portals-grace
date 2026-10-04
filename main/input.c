#include "input.h"
#include <math.h>
#include "bsp/input.h"
#include "esp_log.h"
#include "gl_input.h"
#include "graceloader_imu.h"

#define LOOK_SPEED 2.0f  // radians a second on the cursor keys

static bool held(bsp_input_scancode_t sc) {
    bool state = false;
    return gl_input_read_scancode(sc, &state) == ESP_OK && state;
}

// The cursor keys come as escaped "grey" scancodes from a PC keyboard
// and as navigation keys from the built-in one; ask both.
static bool nav_held(bsp_input_navigation_key_t key) {
    bool state = false;
    return gl_input_read_navigation_key(key, &state) == ESP_OK && state;
}

static float axis(bool neg, bool pos) {
    return (pos ? 1.0f : 0.0f) - (neg ? 1.0f : 0.0f);
}

// --- Gyroscope (after SynthMiner's game/input.c) --------------------------
//
// The RATE gyroscope: the turn rate is added up frame by frame and handed
// to the look like the cursor keys' delta -- turn the badge 30 degrees and
// the view turns 30 degrees. Held upright, device X runs top to bottom
// (turning left / right is about it) and Y runs across (tipping the
// screen is about it).
#define GYRO_YAW_SIGN   (+1.0f)
#define GYRO_PITCH_SIGN (+1.0f)
#define GYRO_GAIN       1.0f
// A resting gyroscope does not read zero. Readings this slow are taken
// as the sensor's own offset and tracked, not turned into a slow spin.
#define GYRO_REST_DPS  3.0f
#define GYRO_BIAS_RATE 0.02f
#define GYRO_MAX_STEP  0.6f

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
    float const k = GYRO_GAIN * dt * (3.14159265f / 180.0f);
    *dyaw += clampf(GYRO_YAW_SIGN * gyro_axis(gx, &s_bias_x) * k, -GYRO_MAX_STEP, GYRO_MAX_STEP);
    *dpitch += clampf(GYRO_PITCH_SIGN * gyro_axis(gy, &s_bias_y) * k, -GYRO_MAX_STEP, GYRO_MAX_STEP);
}

// --- Polling ---------------------------------------------------------

enum { K_Q, K_E, K_R, K_N, K_G, K_H, K_P, K_COUNT };
static bsp_input_scancode_t const s_edge_keys[K_COUNT] = {
    BSP_INPUT_SCANCODE_Q, BSP_INPUT_SCANCODE_E, BSP_INPUT_SCANCODE_R, BSP_INPUT_SCANCODE_N,
    BSP_INPUT_SCANCODE_G, BSP_INPUT_SCANCODE_H, BSP_INPUT_SCANCODE_P,
};
static bool s_last[K_COUNT];

void input_poll(input_frame_t* out, float dt, bool gyro_on) {
    *out = (input_frame_t){0};

    bool pressed[K_COUNT];
    for (int i = 0; i < K_COUNT; i++) {
        bool const now = held(s_edge_keys[i]);
        pressed[i]     = now && !s_last[i];
        s_last[i]      = now;
    }
    out->fire[0] = pressed[K_Q];
    out->fire[1] = pressed[K_E];
    out->restart = pressed[K_R];
    out->next    = pressed[K_N];
    out->gyro    = pressed[K_G];
    out->half    = pressed[K_H];
    out->depth   = pressed[K_P];

    out->fwd    = axis(held(BSP_INPUT_SCANCODE_S), held(BSP_INPUT_SCANCODE_W));
    out->strafe = axis(held(BSP_INPUT_SCANCODE_A), held(BSP_INPUT_SCANCODE_D));
    out->jump   = held(BSP_INPUT_SCANCODE_SPACE);

    float const lx = axis(held(BSP_INPUT_SCANCODE_ESCAPED_GREY_LEFT) || nav_held(BSP_INPUT_NAVIGATION_KEY_LEFT),
                          held(BSP_INPUT_SCANCODE_ESCAPED_GREY_RIGHT) || nav_held(BSP_INPUT_NAVIGATION_KEY_RIGHT));
    float const ly = axis(held(BSP_INPUT_SCANCODE_ESCAPED_GREY_UP) || nav_held(BSP_INPUT_NAVIGATION_KEY_UP),
                          held(BSP_INPUT_SCANCODE_ESCAPED_GREY_DOWN) || nav_held(BSP_INPUT_NAVIGATION_KEY_DOWN));
    out->dyaw   = lx * LOOK_SPEED * dt;
    out->dpitch = ly * LOOK_SPEED * dt;  // positive pitch looks down
    if (gyro_on) gyro(dt, &out->dyaw, &out->dpitch);
}
