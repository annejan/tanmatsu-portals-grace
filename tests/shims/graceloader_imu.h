#pragma once
// Host stand-in: the gyroscope calls input.c makes.
#include <stdbool.h>
#include "esp_err.h"

esp_err_t bsp_orientation_enable_gyroscope(void);
esp_err_t bsp_orientation_get(bool* out_gyro_ready, bool* out_accel_ready, float* out_gyro_x, float* out_gyro_y,
                              float* out_gyro_z, float* out_accel_x, float* out_accel_y, float* out_accel_z);
