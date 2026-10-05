#pragma once
// Host stand-in: the two key reads input.c makes. The real gl_input.h
// sits beside the badge's FreeRTOS headers and pulls those in.
#include <stdbool.h>
#include "bsp/input.h"

esp_err_t gl_input_read_navigation_key(bsp_input_navigation_key_t key, bool* out_state);
esp_err_t gl_input_read_scancode(bsp_input_scancode_t key, bool* out_state);
