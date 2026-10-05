#pragma once
// The badge's user LEDs A and B show the portals: A orange while the
// orange portal is open, B blue while the blue one is. While the game has
// them the LEDs are in manual mode, so the system LEDs (power, radio,
// message) are dark; they go back to the coprocessor on leds_release().

#include <stdbool.h>

void leds_portals(bool blue, bool orange);
void leds_release(void);
