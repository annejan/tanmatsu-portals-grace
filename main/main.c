// =====================================================================
//  Tanmatsu Portal -- a portal puzzle on SynthEngine3D
// =====================================================================

#include <math.h>
#include <stdio.h>
#include "bsp/device.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "graceloader.h"
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
#include "synthengine3d.h"

static char const TAG[] = "portal";

#define MESSAGE_S 2.5f

static level_t        s_lv;
static player_t       s_pl;
static portal_t       s_portals[2];
static int            s_chamber;
static bool           s_half_ok;
static se_ppa_layer_t s_layer;

static char  s_msg[64];
static float s_msg_t;
static int   s_pending_chamber = -1;  // load this once the message is read
static float s_fps;
static int64_t s_render_us;

static void message(char const* text) {
    snprintf(s_msg, sizeof(s_msg), "%s", text);
    s_msg_t = MESSAGE_S;
}

static void load_chamber(int index) {
    s_chamber = index;
    level_load(&s_lv, index);
    player_spawn(&s_pl, &s_lv);
    s_portals[0].open = s_portals[1].open = false;
    render_set_level(&s_lv, s_portals);
    ESP_LOGI(TAG, "chamber %d: %s", index, s_lv.name);
}

static void fire(int which) {
    vec3_t const  eye  = player_eye(&s_pl);
    basis_t const view = player_view(&s_pl);
    portal_t      p;
    if (!portal_place(&s_lv, eye, view.fwd, &s_portals[which ^ 1], &p)) return;
    s_portals[which] = p;
    render_set_level(&s_lv, s_portals);
}

// --- Engine callbacks ---------------------------------------------------

static void on_init(void* user) {
    (void)user;
    static char tex_dir[160];
    snprintf(tex_dir, sizeof(tex_dir), "%s/textures", graceloader_get_install_basepath());
    render_init(tex_dir);
    settings_load();
    input_init();

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
    load_chamber(0);
    message(s_lv.name);
}

static void on_input(bsp_input_event_t const* ev, void* user) {
    (void)user;
    // A menu that is showing has the keyboard, all of it.
    if (menu_active()) {
        menu_event(ev);
    } else if (menu_is_open_key(ev)) {
        menu_open(s_chamber);
    }
}

// The menu's frame: the game stands still underneath it.
static void menu_frame(void) {
    menu_cmd_t const cmd = menu_update();
    switch (cmd.kind) {
        case MENU_CMD_RESTART:
            load_chamber(s_chamber);
            message(s_lv.name);
            break;
        case MENU_CMD_CHAMBER:
            s_pending_chamber = -1;
            load_chamber(cmd.chamber);
            message(s_lv.name);
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
        load_chamber(s_chamber);
        message(s_lv.name);
    }

    if (s_msg_t > 0.0f) s_msg_t -= dt;
    if (s_pending_chamber >= 0 && s_msg_t <= 0.0f) {
        load_chamber(s_pending_chamber);
        s_pending_chamber = -1;
        message(s_lv.name);
        return;
    }
    if (s_pending_chamber >= 0) return;  // the chamber is done; wait out the message

    s_pl.yaw += in.dyaw;
    s_pl.pitch = fmaxf(-PL_PITCH_MAX, fminf(PL_PITCH_MAX, s_pl.pitch + in.dpitch));
    if (in.fire[0]) fire(PORTAL_BLUE);
    if (in.fire[1]) fire(PORTAL_ORANGE);

    player_input_t const pin = {.fwd = in.fwd, .strafe = in.strafe, .jump = in.jump};
    int const            ev  = player_update(&s_pl, &s_lv, s_portals, &pin, dt);
    if (ev & PL_EV_DIED) {
        load_chamber(s_chamber);
        message("Test subject lost. Again.");
    } else if (ev & PL_EV_EXIT) {
        int const next = s_chamber + 1;
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
        if (s_portals[i].open) {
            pax_simple_rect(fb, col, x, cy - 8, 6, 16);
        } else {
            pax_outline_rect(fb, col, x, cy - 8, 6, 16);
        }
    }
    pax_simple_rect(fb, 0xFFFFFFFFu, cx - 1, cy - 1, 2, 2);

    pax_draw_text(fb, 0xFFFFFFFFu, pax_font_sky_mono, 16, 8, 6, s_lv.name);
    pax_draw_text(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, 8, 26, s_lv.hint);

    // The keys as they are bound now, not as they shipped.
    char blue[16], orange[16], help[96];
    snprintf(help, sizeof(help), "%s blue   %s orange   Esc menu", input_key_name(input_key(ACT_BLUE), blue, sizeof(blue)),
             input_key_name(input_key(ACT_ORANGE), orange, sizeof(orange)));
    pax_vec2f const hs = pax_text_size(pax_font_sky_mono, 12, help);
    pax_draw_text(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, DISPLAY_LOG_W - 8 - hs.x, 6, help);

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
    bool const       half   = settings_half_res() && s_half_ok;
    pax_buf_t* const target = half ? &s_layer.buf : fb;
    scene_set_render_scale(half ? 2 : 1);

    int64_t const t0 = esp_timer_get_time();
    render_frame(target, &s_lv, &s_pl, s_portals);
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
