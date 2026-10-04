#pragma once
// The keyboard, polled once a frame through the engine's remappable
// bindings (se_bindings), and the gyroscope.

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ACT_FORWARD = 0,
    ACT_BACK,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_JUMP,
    ACT_BLUE,
    ACT_ORANGE,
    ACT_USE,
    ACT_LOOK_UP,
    ACT_LOOK_DOWN,
    ACT_LOOK_LEFT,
    ACT_LOOK_RIGHT,
    ACT_RESTART,
    ACT_GYRO,
    ACT_COUNT,
} action_t;

typedef struct {
    float fwd, strafe;   // -1 .. 1
    float dyaw, dpitch;  // radians to turn this frame: keys plus gyro
    bool  jump;          // held
    // Pressed this frame:
    bool fire[2];
    bool use;
    bool restart;
    bool gyro;
} input_frame_t;

void input_init(void);  // after the engine is up: loads the bindings
void input_poll(input_frame_t* out, float dt, bool gyro_on);
// Forget what was held, so a key still down when a menu closes does not
// count as pressed again.
void input_resync(void);

char const* input_action_label(action_t a);
uint16_t    input_key(action_t a);
void        input_bind(action_t a, uint16_t sc);
void        input_reset_defaults(void);
// A key's name for the screen: "Q", "Space", "Up", ...
char const* input_key_name(uint16_t sc, char* buf, int cap);
