#pragma once
// The keyboard, polled once a frame, and the gyroscope.

#include <stdbool.h>

typedef struct {
    float fwd, strafe;  // -1 .. 1
    float dyaw, dpitch; // radians to turn this frame: keys plus gyro
    bool  jump;         // held
    // Pressed this frame:
    bool fire[2];       // Q blue, E orange
    bool restart;       // R
    bool next;          // N, skip the chamber
    bool gyro;          // G, gyroscope on / off
    bool half;          // H, quarter-resolution on / off
    bool depth;         // P, how deep the views through portals go
} input_frame_t;

void input_poll(input_frame_t* out, float dt, bool gyro_on);
