// =====================================================================
//  Tanmatsu Portal -- a portal puzzle on SynthEngine3D
// =====================================================================

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bsp/device.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "graceloader.h"
#include "chamber.h"
#include "demo.h"
#include "editor.h"
#include "input.h"
#include "level.h"
#include "menu.h"
#include "pax_fonts.h"
#include "pax_gfx.h"
#include "pax_text.h"
#include "player.h"
#include "portal.h"
#include "render.h"
#include "settings.h"
#include "sound.h"
#include "synthengine3d.h"
#include "testkit/devtest.h"
#include "testkit/showtime.h"

static char const TAG[] = "portal";

#define MESSAGE_S   2.5f
#define CHAMBER_DIR "/sd/portals/chambers"

static game_t         s_game;  // the chamber in play: level, player, portals, cubes
static bool           s_half_ok;
static se_ppa_layer_t s_layer;

static char  s_msg[64];
static float s_msg_t;
static int   s_pending_chamber = -1;  // load this once the message is read

// Playing the chambers, editing one, or play-testing the one being edited.
typedef enum { MODE_PLAY, MODE_EDIT, MODE_TEST } app_mode_t;
static app_mode_t s_mode;
static int    s_play_chamber;  // where play goes back to after the editor
static bool   s_test_back;     // Esc in a play-test: back to the editor
static float  s_test_done;     // the play-test reached the exit: back after a moment
static float s_fps;
static int   s_frames;
static float s_period_t, s_period_ms;

// A scripted demo (main/demo.c) playing instead of the player: what the
// device tests select. A pure function of show time.
static int    s_demo = -1;
static double s_demo_t0;
static int64_t s_render_us;

static void message(char const* text) {
    snprintf(s_msg, sizeof(s_msg), "%s", text);
    s_msg_t = MESSAGE_S;
}

static void load_chamber(int index) {
    game_load(&s_game, index);
    render_set_level(&s_game.lv, s_game.portals);
    ESP_LOGI(TAG, "chamber %d: %s", index, s_game.lv.name);
}

static bool same_portal(portal_t const* a, portal_t const* b) {
    return a->open == b->open && (!a->open || (a->face == b->face && memcmp(a->cell, b->cell, sizeof(a->cell)) == 0));
}

// --- Device tests (main/testkit) -------------------------------------------

static bool test_select(char const* name) {
    int const i = demo_find(name);
    if (i < 0) return false;
    s_demo    = i;
    s_demo_t0 = showtime_now();
    s_msg_t   = 0.0f;
    return true;
}
static float test_duration(void) {
    return demo_duration(s_demo);
}
static double test_started(void) {
    return s_demo_t0;
}
static char const* test_name(void) {
    return s_demo >= 0 ? demo_name(s_demo) : "play";
}
static char const* test_shot_name(void) {
    return "";
}

static devtest_content_t const TEST_CONTENT = {
    .select    = test_select,
    .duration  = test_duration,
    .started   = test_started,
    .name      = test_name,
    .shot_name = test_shot_name,
};
static devtest_config_t const TEST = {
    .app = "com.annejan.portals", .shot_dir = "/sd/portals/test", .content = &TEST_CONTENT,
};

// The demo's state at this show time, in place of the player's.
static void demo_frame(void) {
    static demo_state_t st;
    demo_eval(s_demo, (float)(showtime_now() - s_demo_t0), &st);
    bool const remesh = st.g.chamber != s_game.chamber || !same_portal(&st.g.portals[0], &s_game.portals[0]) ||
                        !same_portal(&st.g.portals[1], &s_game.portals[1]);
    s_game = st.g;
    if (remesh) render_set_level(&s_game.lv, s_game.portals);
}

// --- Engine callbacks ---------------------------------------------------

static void on_init(void* user) {
    (void)user;
    static char tex_dir[160];
    snprintf(tex_dir, sizeof(tex_dir), "%s/textures", graceloader_get_install_basepath());
    render_init(tex_dir);
    settings_load();
    input_init();
    // The player's own chambers, after the built-in ones.
    int const own = chamber_load_dir(CHAMBER_DIR);
    if (own > 0) ESP_LOGI(TAG, "%d chamber(s) from %s", own, CHAMBER_DIR);

    // The half-size layer a quarter-resolution frame draws into.
    s_half_ok = false;
    if (se_ppa_init()) {
        se_display_info_t di;
        se_display_info(&di);
        s_half_ok = se_ppa_layer_alloc(&s_layer, DISPLAY_LOG_W / 2, DISPLAY_LOG_H / 2, di.pax_format, di.reversed,
                                       di.orientation);
    }
    if (!s_half_ok) ESP_LOGW(TAG, "no quarter-resolution layer; drawing at full resolution");

    se_splash_ex("PORTALS", "for Tanmatsu", 1.0f);
    sound_init();
    sound_set_music(settings_music());
    sound_set_effects(settings_effects());
    load_chamber(0);
    message(s_game.lv.name);
    devtest_start(&TEST);
}

static void start_playtest(void) {
    game_load_level(&s_game, editor_level());
    render_set_level(&s_game.lv, s_game.portals);
    s_mode      = MODE_TEST;
    s_test_back = false;
    s_test_done = 0.0f;
    message("Play-test: Esc goes back to the editor");
    input_resync();
}

static void back_to_editor(void) {
    s_mode = MODE_EDIT;
    editor_resume();
}

static void on_input(bsp_input_event_t const* ev, void* user) {
    (void)user;
    if (s_mode == MODE_EDIT) {
        editor_event(ev);
        return;
    }
    if (s_mode == MODE_TEST) {
        if (menu_is_open_key(ev)) s_test_back = true;
        return;
    }
    // A menu that is showing has the keyboard, all of it.
    if (menu_active()) {
        menu_event(ev);
    } else if (menu_is_open_key(ev)) {
        menu_open(s_game.chamber);
    }
}

// The menu's frame: the game stands still underneath it.
static void menu_frame(void) {
    menu_cmd_t const cmd = menu_update();
    switch (cmd.kind) {
        case MENU_CMD_RESTART:
            load_chamber(s_game.chamber);
            message(s_game.lv.name);
            break;
        case MENU_CMD_CHAMBER:
            s_pending_chamber = -1;
            load_chamber(cmd.chamber);
            message(s_game.lv.name);
            break;
        case MENU_CMD_EDITOR:
            s_play_chamber = s_game.chamber;
            editor_open(s_game.chamber, CHAMBER_DIR);
            s_mode = MODE_EDIT;
            break;
        case MENU_CMD_QUIT: bsp_device_restart_to_launcher(); break;
        default: break;
    }
    // Keys still held from the menu do not fire on the way out.
    if (!menu_active()) input_resync();
}

static void on_update(float dt, void* user) {
    (void)user;
    if (dt > 0.0f) s_fps += (1.0f / dt - s_fps) * 0.1f;
    showtime_frame();
    devtest_update();
    if (s_demo >= 0) {
        demo_frame();
        return;
    }
    if (s_mode == MODE_EDIT) {
        editor_cmd_t const c = editor_update(dt);
        if (c == EDITOR_CMD_PLAYTEST) start_playtest();
        if (c == EDITOR_CMD_QUIT) {
            s_mode = MODE_PLAY;
            load_chamber(s_play_chamber < level_count() ? s_play_chamber : 0);
            message(s_game.lv.name);
            input_resync();
        }
        return;
    }
    if (s_mode == MODE_TEST && (s_test_back || (s_test_done > 0.0f && (s_test_done -= dt) <= 0.0f))) {
        back_to_editor();
        return;
    }
    if (menu_active()) {
        menu_frame();
        return;
    }

    input_frame_t in;
    input_poll(&in, dt, settings_gyro());

    if (in.gyro) {
        settings_set_gyro(!settings_gyro());
        message(settings_gyro() ? "Gyroscope on" : "Gyroscope off");
    }
    if (in.restart) {
        if (s_mode == MODE_TEST) {
            start_playtest();
        } else {
            load_chamber(s_game.chamber);
            message(s_game.lv.name);
        }
    }

    if (s_msg_t > 0.0f) s_msg_t -= dt;
    if (s_pending_chamber >= 0 && s_msg_t <= 0.0f) {
        load_chamber(s_pending_chamber);
        s_pending_chamber = -1;
        message(s_game.lv.name);
        return;
    }
    if (s_pending_chamber >= 0) return;  // the chamber is done; wait out the message

    game_input_t const gin = {
        .fwd    = in.fwd,
        .strafe = in.strafe,
        .dyaw   = in.dyaw,
        .dpitch = in.dpitch,
        .jump   = in.jump,
        .fire   = {in.fire[0], in.fire[1]},
        .use    = in.use,
    };
    int const ev = game_step(&s_game, &gin, dt);
    sound_events(ev);
    if (ev & GAME_EV_PORTAL) render_set_level(&s_game.lv, s_game.portals);
    if (s_mode == MODE_TEST) {
        if (ev & PL_EV_DIED) {
            start_playtest();
            message("Test subject lost. Again.");
        } else if ((ev & PL_EV_EXIT) && s_test_done <= 0.0f) {
            message("It can be solved. Back to the editor...");
            s_test_done = MESSAGE_S;
        }
        return;
    }
    if (ev & PL_EV_DIED) {
        load_chamber(s_game.chamber);
        message("Test subject lost. Again.");
    } else if (ev & PL_EV_EXIT) {
        int const next = s_game.chamber + 1;
        message(next < level_count() ? "Chamber complete" : "All chambers complete. Cake later.");
        s_pending_chamber = next % level_count();
    }
}

static void on_backdrop(pax_buf_t* fb, void* user) {
    (void)fb;
    (void)user;  // every pixel is drawn by the passes; nothing to clear
}

static void hud(pax_buf_t* fb) {
    float const cx = RENDER_HALF_W, cy = RENDER_HORIZON_Y;
    // The crosshair: blue half left, orange half right, filled when placed.
    for (int i = 0; i < 2; i++) {
        uint32_t const col = i == 0 ? 0xFF2C8CFFu : 0xFFFF8A1Cu;
        float const    x   = i == 0 ? cx - 12 : cx + 6;
        if (s_game.portals[i].open) {
            pax_simple_rect(fb, col, x, cy - 8, 6, 16);
        } else {
            pax_outline_rect(fb, col, x, cy - 8, 6, 16);
        }
    }
    pax_simple_rect(fb, 0xFFFFFFFFu, cx - 1, cy - 1, 2, 2);

    pax_draw_text(fb, 0xFFFFFFFFu, pax_font_sky_mono, 16, 8, 6, s_game.lv.name);
    pax_draw_text(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, 8, 26, s_game.lv.hint);

    // The keys as they are bound now, not as they shipped.
    char blue[16], orange[16], use[16], help[112];
    snprintf(help, sizeof(help), "%s blue   %s orange   %s use   Esc menu",
             input_key_name(input_key(ACT_BLUE), blue, sizeof(blue)),
             input_key_name(input_key(ACT_ORANGE), orange, sizeof(orange)),
             input_key_name(input_key(ACT_USE), use, sizeof(use)));
    pax_vec2f const hs = pax_text_size(pax_font_sky_mono, 12, help);
    pax_draw_text(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, DISPLAY_LOG_W - 8 - hs.x, 6, help);

    if (s_demo >= 0) return;  // a device test's shots: nothing that varies run to run
    int  passes, tris;
    char stat[64];
    render_stats(&passes, &tris);
    snprintf(stat, sizeof(stat), "%2.0f fps %3lld ms  %d pass %d tri%s%s", s_fps, s_render_us / 1000, passes, tris,
             settings_half_res() && s_half_ok ? "  half" : "", settings_gyro() ? "  gyro" : "");
    pax_draw_text(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, 8, DISPLAY_LOG_H - 18, stat);

    if (s_msg_t > 0.0f) {
        pax_vec2f const sz = pax_text_size(pax_font_sky_mono, 24, s_msg);
        pax_draw_text(fb, 0xFFFFFFFFu, pax_font_sky_mono, 24, cx - sz.x * 0.5f, cy - 70, s_msg);
    }
}

static void on_render(pax_buf_t* fb, void* user) {
    (void)user;
    if (s_mode == MODE_EDIT) {
        editor_draw(fb);
        return;
    }
    bool const       half   = settings_half_res() && s_half_ok;
    pax_buf_t* const target = half ? &s_layer.buf : fb;
    scene_set_render_scale(half ? 2 : 1);

    int64_t const t0 = esp_timer_get_time();
    render_frame(target, &s_game);
    if (half) {
        // The CPU's pixels to PSRAM before the PPA's DMA reads them.
        se_ppa_layer_sync(&s_layer);
        if (se_ppa_blit_scaled(fb, 0, &s_layer, 2)) {
            se_ppa_wait_job(0);
            se_ppa_buf_invalidate(fb);  // the HUD draws on top with the CPU
        }
    }
    s_render_us = esp_timer_get_time() - t0;
    hud(fb);
    menu_draw(fb);
    devtest_after_render(fb, s_render_us);

    // Once a second: the device test's PERF record, if one is running.
    static int64_t last_us;
    int64_t const  now_us = esp_timer_get_time();
    if (last_us != 0) {
        s_period_t += (float)(now_us - last_us) / 1e6f;
        s_frames++;
        if (s_period_t >= 1.0f) {
            s_period_ms = s_period_t * 1000.0f / (float)s_frames;
            devtest_period((float)s_frames / s_period_t, s_period_ms);
            s_period_t = 0.0f;
            s_frames   = 0;
        }
    }
    last_us = now_us;
}

void app_main(void) {
    static se_app_config_t const cfg = {
        .f1_exits      = true,
        .backdrop_argb = 0xFF000000u,
    };
    static se_app_callbacks_t const cb = {
        .on_init     = on_init,
        .on_input    = on_input,
        .on_update   = on_update,
        .on_backdrop = on_backdrop,
        .on_render   = on_render,
    };
    se_run(&cfg, &cb, NULL);
}
