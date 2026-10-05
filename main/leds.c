#include "leds.h"
#include <stdint.h>
#include "bsp/led.h"

#define LED_ORANGE 0xFF6A00u
#define LED_BLUE   0x0060FFu

static bool s_manual;      // the game has the LEDs
static int  s_shown = -1;  // what they show: bit 0 blue, bit 1 orange

void leds_portals(bool blue, bool orange) {
    int const want = (blue ? 1 : 0) | (orange ? 2 : 0);
    if (s_manual && want == s_shown) return;  // the coprocessor sits on I2C: only on a change
    uint32_t n = 0;
    if (bsp_led_get_count(&n) != ESP_OK || n < 2) return;
    if (!s_manual) {
        if (bsp_led_set_mode(false) != ESP_OK) return;
        s_manual = true;
    }
    // The user LEDs are the last two: A, then B.
    for (uint32_t i = 0; i < n; i++) bsp_led_set_pixel(i, 0);
    bsp_led_set_pixel(n - 2, orange ? LED_ORANGE : 0);
    bsp_led_set_pixel(n - 1, blue ? LED_BLUE : 0);
    bsp_led_send();
    s_shown = want;
}

void leds_release(void) {
    if (!s_manual) return;
    bsp_led_clear();
    bsp_led_set_mode(true);
    s_manual = false;
    s_shown  = -1;
}
