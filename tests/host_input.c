// Host tests for main/input.c: the keyboard, the bindings and the
// gyroscope are stubbed, and the presses the game sees are checked. Built
// against tests/shims for the few ESP-IDF headers bsp/input.h wants.
// `make check`.

#include "../main/input.c"

static int s_fail;

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            s_fail++;                                   \
        }                                               \
    } while (0)

// --- Stubs --------------------------------------------------------------

static bool     s_down[0x200];  // scancodes held right now
static bool     s_nav_down[16];
static uint16_t s_bind[ACT_COUNT];

esp_err_t gl_input_read_scancode(bsp_input_scancode_t key, bool* out_state) {
    *out_state = s_down[key & 0x1ff];
    return ESP_OK;
}

esp_err_t gl_input_read_navigation_key(bsp_input_navigation_key_t key, bool* out_state) {
    *out_state = (unsigned)key < 16 && s_nav_down[key];
    return ESP_OK;
}

void se_bindings_init(se_bindings_config_t const* cfg) {
    for (int i = 0; i < cfg->count; i++) s_bind[cfg->defs[i].id] = cfg->defs[i].default_sc;
}

uint16_t se_bindings_get(int id) {
    return id >= 0 && id < ACT_COUNT ? s_bind[id] : 0;
}

void se_bindings_set(int id, uint16_t sc) {
    if (id >= 0 && id < ACT_COUNT && sc != 0) s_bind[id] = sc;
}

esp_err_t bsp_orientation_enable_gyroscope(void) {
    return ESP_FAIL;
}

esp_err_t bsp_orientation_get(bool* gr, bool* ar, float* gx, float* gy, float* gz, float* ax, float* ay, float* az) {
    (void)gr, (void)ar, (void)gx, (void)gy, (void)gz, (void)ax, (void)ay, (void)az;
    return ESP_FAIL;
}

// --- Helpers ------------------------------------------------------------

static void key_event(uint16_t sc, bool down) {
    bsp_input_event_t ev      = {.type = INPUT_EVENT_TYPE_SCANCODE};
    ev.args_scancode.scancode = (bsp_input_scancode_t)(down ? sc : sc | BSP_INPUT_SCANCODE_RELEASE_MODIFIER);
    input_event(&ev);
}

static void nav_event(bsp_input_navigation_key_t key, bool down) {
    bsp_input_event_t ev     = {.type = INPUT_EVENT_TYPE_NAVIGATION};
    ev.args_navigation.key   = key;
    ev.args_navigation.state = down;
    input_event(&ev);
}

static input_frame_t poll(void) {
    input_frame_t f;
    input_poll(&f, 1.0f / 15.0f, false);
    return f;
}

// --- Tests --------------------------------------------------------------

// A key that goes down and up again between two frames still fires once.
static void test_tap_between_frames(void) {
    input_resync();
    CHECK(!poll().fire[0], "nothing pressed, yet a shot");
    key_event(BSP_INPUT_SCANCODE_Q, true);
    key_event(BSP_INPUT_SCANCODE_Q, false);  // never seen held by a poll
    CHECK(poll().fire[0], "a tap between two frames was lost");
    CHECK(!poll().fire[0], "one tap, two shots");
}

// Held across frames: one press, however many frames it stays down.
static void test_hold_fires_once(void) {
    input_resync();
    s_down[BSP_INPUT_SCANCODE_E] = true;
    key_event(BSP_INPUT_SCANCODE_E, true);
    input_frame_t f = poll();
    CHECK(f.fire[1] && !f.fire[0], "held E: orange %d blue %d", f.fire[1], f.fire[0]);
    for (int i = 0; i < 5; i++) CHECK(!poll().fire[1], "still held, frame %d: a second shot", i);
    s_down[BSP_INPUT_SCANCODE_E] = false;
    key_event(BSP_INPUT_SCANCODE_E, false);
    CHECK(!poll().fire[1], "a release fired");
    // Down and up and down again in one frame: still one press for the frame.
    key_event(BSP_INPUT_SCANCODE_E, true);
    key_event(BSP_INPUT_SCANCODE_E, false);
    key_event(BSP_INPUT_SCANCODE_E, true);
    s_down[BSP_INPUT_SCANCODE_E] = true;
    CHECK(poll().fire[1], "pressed twice in a frame: no shot");
    CHECK(!poll().fire[1], "pressed twice in a frame: shots in two frames");
    s_down[BSP_INPUT_SCANCODE_E] = false;
}

// Only the key the action is bound to; a rebinding moves the latch with it.
static void test_bindings(void) {
    input_resync();
    key_event(BSP_INPUT_SCANCODE_X, true);
    input_frame_t f = poll();
    CHECK(!f.fire[0] && !f.fire[1] && !f.use && !f.restart && !f.gyro, "an unbound key did something");
    input_bind(ACT_USE, BSP_INPUT_SCANCODE_X);
    key_event(BSP_INPUT_SCANCODE_X, true);
    CHECK(poll().use, "use, bound to X, missed a tap of X");
    key_event(BSP_INPUT_SCANCODE_F, true);
    CHECK(!poll().use, "F still uses after use moved to X");
    input_reset_defaults();
}

// The built-in keyboard's cursor keys come as navigation events; bound to
// the grey cursor scancodes, they latch like any key.
static void test_navigation_keys(void) {
    input_resync();
    input_bind(ACT_RESTART, BSP_INPUT_SCANCODE_ESCAPED_GREY_UP);
    nav_event(BSP_INPUT_NAVIGATION_KEY_UP, true);
    nav_event(BSP_INPUT_NAVIGATION_KEY_UP, false);
    CHECK(poll().restart, "a tap of the built-in up key, bound to restart, was lost");
    nav_event(BSP_INPUT_NAVIGATION_KEY_DOWN, true);
    CHECK(!poll().restart, "down restarted");
    input_reset_defaults();
}

// A resync -- after the menu, after a chamber loads -- forgets taps from
// before it, as it forgets keys held through it.
static void test_resync_clears(void) {
    input_resync();
    key_event(BSP_INPUT_SCANCODE_Q, true);
    input_resync();
    CHECK(!poll().fire[0], "a tap from before the resync fired after it");
    s_down[BSP_INPUT_SCANCODE_Q] = true;
    input_resync();
    CHECK(!poll().fire[0], "a key held through the resync fired");
    s_down[BSP_INPUT_SCANCODE_Q] = false;
}

int main(void) {
    input_init();
    test_tap_between_frames();
    test_hold_fires_once();
    test_bindings();
    test_navigation_keys();
    test_resync_clears();
    if (s_fail) {
        printf("input tests: %d failed\n", s_fail);
        return 1;
    }
    printf("input tests: all passed\n");
    return 0;
}
