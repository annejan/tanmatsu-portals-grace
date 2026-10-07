// =====================================================================
//  Tanmatsu Portal -- a portal puzzle on SynthEngine3D
// =====================================================================
//
// The app: the engine's callbacks, and what is on -- the title screen
// (attract.c behind menu.c), play, the editor and its play-tests, a
// recording being watched (watch.c), or a device test. hud.c draws what
// goes over the chamber.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "app.h"
#include "attract.h"
#include "bsp/device.h"
#include "chamber.h"
#include "demo.h"
#include "editor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "graceloader.h"
#include "hud.h"
#include "input.h"
#include "leds.h"
#include "level.h"
#include "menu.h"
#include "nvs_settings_owner.h"
#include "pax_gfx.h"
#include "player.h"
#include "portal.h"
#include "recording.h"
#include "render.h"
#include "settings.h"
#include "sound.h"
#include "synthengine3d.h"
#include "testkit/devtest.h"
#include "testkit/showtime.h"
#include "watch.h"

static char const TAG[] = "portal";

#define CHAMBER_DIR "/sd/portals/chambers"

static game_t         s_game;  // the chamber in play: level, player, portals, cubes
static bool           s_half_ok;
static se_ppa_layer_t s_layer;

static int s_pending_chamber = -1;  // load this once the message is read

// Playing the chambers, editing one, or play-testing the one being edited.
typedef enum {
    MODE_PLAY,
    MODE_EDIT,
    MODE_TEST
} app_mode_t;
static app_mode_t s_mode;
static char       s_play_id[64];  // the chamber play goes back to after the editor
static bool       s_edit_title;   // the editor was opened from the title screen, and goes back to it
static bool       s_test_back;    // Esc in a play-test: back to the editor
static float      s_test_done;    // the play-test reached the exit: back after a moment
static float      s_fps;
static int        s_frames;
static float      s_period_t, s_period_ms;

// A scripted demo (main/demo.c) playing instead of the player: what the
// device tests select. A pure function of show time.
static int     s_demo = -1;
static double  s_demo_t0;
static int64_t s_render_us;

// Which chamber's story was told last: a restart does not tell it again.
static int s_story_of = -2;

// "[Subject-Name-here]" in a story line is the badge owner's nickname, if
// they have set one in the launcher; otherwise the joke stands as written.
static void personalise(char* story, size_t n) {
    static char const token[] = "[Subject-Name-here]";
    char* const       at      = strstr(story, token);
    if (at == NULL) return;
    // Read it straight: nvs_settings_get_owner_nickname_configured() says
    // no on every badge (it asks with no buffer, which the helper behind it
    // refuses), so the nickname's own read is the test.
    char name[64];
    if (nvs_settings_get_owner_nickname(name, sizeof(name), "") != ESP_OK || name[0] == '\0') return;
    // A long nickname in a long line would push its end off: the joke stands.
    if (strlen(story) - strlen(token) + strlen(name) >= n) return;
    char out[sizeof(s_game.lv.story)];
    snprintf(out, sizeof(out), "%.*s%s%s", (int)(at - story), story, name, at + strlen(token));
    snprintf(story, n, "%s", out);
}

static void load_chamber(int index) {
    bool const fresh = index != s_story_of;  // a restart does not tell it again
    if (fresh) hud_story_start();
    s_story_of = index;
    game_load(&s_game, index);
    personalise(s_game.lv.story, sizeof(s_game.lv.story));
    if (fresh) sound_say(s_game.lv.story[0] ? s_game.lv.story : NULL);
    render_set_level(&s_game.lv, s_game.portals);
    ESP_LOGI(TAG, "chamber %d: %s", index, s_game.lv.name);
}

// Play moves on to chamber `index`: where Continue, on the title screen,
// comes back to.
static void play_chamber(int index) {
    s_pending_chamber = -1;
    load_chamber(index);
    hud_message(s_game.lv.name);
    settings_set_chamber(chamber_id(index));
}

// --- Recording a run ------------------------------------------------------
//
// Esc -> Record from here (or Record a run, on the title): what you press,
// frame by frame, from the chamber's start, through every chamber you get
// through, until Stop recording or until play goes elsewhere. A death or
// a restart starts the chamber's recording over, so a run keeps only the
// clean ones. Saved to RECORDING_DIR/run-NN.txt, for Watch a recording.

#if __has_include("app_version.h")
#include "app_version.h"
#endif
#ifndef APP_VERSION
#define APP_VERSION ""
#endif
#ifndef APP_GIT_HASH
#define APP_GIT_HASH "unknown"
#endif

static recording_capture_t s_cap;
static bool                s_capturing;

// The recording so far, saved; and recording stops.
static void record_stop(void) {
    if (!s_capturing) return;
    s_capturing = false;
    menu_set_recording(false);
    int const kept = s_cap.r.n;
    if (kept == 0) {
        hud_message("Nothing recorded: no chamber finished");
        recording_capture_free(&s_cap);
        return;
    }
    mkdir("/sd/portals", 0755);
    mkdir(RECORDING_DIR, 0755);
    char path[96], name[RECORDING_NAME_N], nick[32];
    int  k = 1;
    for (; k < 100; k++) {  // the first run-NN not taken
        snprintf(path, sizeof(path), "%s/run-%02d.txt", RECORDING_DIR, k);
        FILE* f = fopen(path, "r");
        if (f == NULL) break;
        fclose(f);
    }
    if (nvs_settings_get_owner_nickname(nick, sizeof(nick), "") == ESP_OK && nick[0] != '\0')
        snprintf(name, sizeof(name), "%.24s, run %02d", nick, k);
    else
        snprintf(name, sizeof(name), "Run %02d", k);
    char msg[HUD_MESSAGE_N];
    if (k < 100 && recording_capture_write(&s_cap, path, name, APP_VERSION " " APP_GIT_HASH))
        snprintf(msg, sizeof(msg), "Saved run-%02d: %d chamber%s", k, kept, kept == 1 ? "" : "s");
    else
        snprintf(msg, sizeof(msg), "Could not save the recording");
    ESP_LOGI(TAG, "%s (%s)", msg, path);
    hud_message(msg);
    recording_capture_free(&s_cap);
}

// Recording starts, from chamber `index`'s start -- or, at -1, a new game's.
static void record_start(int index) {
    if (index < 0) {
        sound_restart_music();
        s_story_of = -2;
        index      = 0;
    }
    play_chamber(index);
    recording_capture_start(&s_cap);
    recording_capture_chamber(&s_cap, chamber_id(index));
    s_capturing = true;
    menu_set_recording(true);
    hud_message("Recording");
}

// The chamber in play, again from its start.
static void restart_chamber(void) {
    s_pending_chamber = -1;
    load_chamber(s_game.chamber);
    hud_message(s_game.lv.name);
    // Its recording from the start again -- or, restarted after its exit,
    // recorded once more.
    if (s_capturing) recording_capture_chamber(&s_cap, chamber_id(s_game.chamber));
}

// To the title screen, the chambers playing behind it.
static void to_title(void) {
    record_stop();
    sound_hush();            // GLaDOS stops mid-sentence ...
    s_story_of        = -2;  // ... and tells the chamber's story again on the way back in
    s_pending_chamber = -1;
    hud_quiet();
    menu_title(chamber_find(settings_chamber()));
    attract_begin(&s_game);
    render_set_level(&s_game.lv, s_game.portals);
}

// --- For watch.c (app.h) ------------------------------------------------------

game_t* app_game(void) {
    return &s_game;
}
void app_load_chamber(int index) {
    load_chamber(index);
}
void app_tell_again(void) {
    s_story_of = -2;
}
void app_to_title(void) {
    to_title();
}
float app_fps(void) {
    return s_fps;
}

// --- Device tests (main/testkit) -------------------------------------------

static bool same_portal(portal_t const* a, portal_t const* b) {
    return a->open == b->open && (!a->open || (a->face == b->face && memcmp(a->cell, b->cell, sizeof(a->cell)) == 0));
}

static bool test_select(char const* name) {
    int const i = demo_find(name);
    if (i < 0) return false;
    menu_close();         // the title screen, at start-up: not in the test's shots ...
    s_game.chamber = -1;  // ... nor the mesh of the chamber playing behind it
    s_demo         = i;
    s_demo_t0      = showtime_now();
    hud_quiet();
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
    .app      = "com.annejan.portals",
    .shot_dir = "/sd/portals/test",
    .content  = &TEST_CONTENT,
};

// The demo's state at this show time, in place of the player's.
static void demo_frame(void) {
    static demo_state_t st;
    demo_eval(s_demo, (float)(showtime_now() - s_demo_t0), &st);
    bool const remesh = st.g.chamber != s_game.chamber || !same_portal(&st.g.portals[0], &s_game.portals[0]) ||
                        !same_portal(&st.g.portals[1], &s_game.portals[1]);
    s_game            = st.g;
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
    sound_set_voice(settings_voice());
    attract_seed((uint32_t)esp_timer_get_time());
    to_title();
    devtest_start(&TEST);
}

static void start_playtest(void) {
    s_pending_chamber = -1;
    game_load_level(&s_game, editor_level());
    personalise(s_game.lv.story, sizeof(s_game.lv.story));
    hud_story_start();
    s_story_of = -1;
    sound_say(s_game.lv.story[0] ? s_game.lv.story : NULL);
    render_set_level(&s_game.lv, s_game.portals);
    s_mode      = MODE_TEST;
    s_test_back = false;
    s_test_done = 0.0f;
    hud_message("Play-test: Esc goes back to the editor");
    input_resync();
}

static void back_to_editor(void) {
    s_mode = MODE_EDIT;
    editor_resume();
}

// Back to the launcher: a recording saved first.
static void quit(void) {
    record_stop();
    sound_say(NULL);
    leds_release();          // the system LEDs back to the coprocessor
    audio_mixer_shutdown();  // se_audio.h: before the restart, or the speaker buzzes
    bsp_device_restart_to_launcher();
}

static void on_input(bsp_input_event_t const* ev, void* user) {
    (void)user;
    // F1: back to the launcher, from anywhere -- here, not in the engine
    // (f1_exits), so a recording is saved first.
    if (ev->type == INPUT_EVENT_TYPE_NAVIGATION && ev->args_navigation.state &&
        ev->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_F1) {
        quit();
        return;
    }
    if (s_mode == MODE_EDIT) {
        editor_event(ev);
        return;
    }
    if (s_mode == MODE_TEST) {
        if (menu_is_open_key(ev)) s_test_back = true;
        input_event(ev);
        return;
    }
    // Watching a recording, Esc stops it; nothing else plays.
    if (watch_on()) {
        if (menu_is_open_key(ev)) watch_stop();
        return;
    }
    // A menu that is showing has the keyboard, all of it.
    if (menu_active()) {
        menu_event(ev);
    } else if (menu_is_open_key(ev)) {
        // The built-in keyboard's second Esc, after the one that stopped a
        // recording, does not open the menu as well.
        if (!watch_just_stopped()) menu_open(s_game.chamber);
    } else {
        input_event(ev);
    }
}

// The menu's frame: the game stands still underneath it.
static void menu_frame(void) {
    bool const       title = menu_on_title();
    menu_cmd_t const cmd   = menu_update();
    switch (cmd.kind) {
        case MENU_CMD_RESTART:
            restart_chamber();
            break;
        case MENU_CMD_CHAMBER:
            record_stop();
            play_chamber(cmd.chamber);
            break;
        case MENU_CMD_RECORD:
            record_stop();
            record_start(cmd.chamber);
            break;
        case MENU_CMD_RECORD_STOP:
            record_stop();
            break;
        case MENU_CMD_NEW_GAME:
            record_stop();
            // From the start: the music from its first bar, and the first
            // chamber's story told.
            sound_restart_music();
            s_story_of = -2;
            play_chamber(0);
            break;
        case MENU_CMD_EDITOR:
            // By name: saving in the editor re-reads the SD card, and the
            // list's order -- its indices -- may change.
            record_stop();
            snprintf(s_play_id, sizeof(s_play_id), "%s", chamber_id(cmd.chamber));
            s_pending_chamber = -1;
            s_edit_title      = title;
            editor_open(cmd.chamber, CHAMBER_DIR);
            s_mode = MODE_EDIT;
            break;
        case MENU_CMD_WATCH:
            record_stop();
            s_pending_chamber = -1;
            if (!watch_start(cmd.recording, title, chamber_id(s_game.chamber)) && title) {
                char err[HUD_MESSAGE_N];
                snprintf(err, sizeof(err), "%s", hud_message_text());
                to_title();
                hud_message(err);
            }
            break;
        case MENU_CMD_TITLE:
            to_title();
            break;
        case MENU_CMD_QUIT:
            quit();
            break;
        default:
            break;
    }
    // Keys still held from the menu do not fire on the way out.
    if (!menu_active()) input_resync();
}

// The editor's frame, and the way out of it.
static void edit_frame(float dt) {
    editor_cmd_t const c = editor_update(dt);
    if (c == EDITOR_CMD_PLAYTEST) start_playtest();
    if (c != EDITOR_CMD_QUIT) return;
    s_mode = MODE_PLAY;
    attract_recount();  // saving re-read the card: the chambers, counted again
    if (s_edit_title) {
        to_title();
    } else {
        int const at = chamber_find(s_play_id);
        load_chamber(at >= 0 ? at : 0);
        hud_message(s_game.lv.name);
    }
    input_resync();
}

// A frame of play, or of a play-test.
static void play_frame(float dt) {
    input_frame_t in;
    input_poll(&in, dt, settings_gyro());

    if (in.gyro) {
        settings_set_gyro(!settings_gyro());
        hud_message(settings_gyro() ? "Gyroscope on" : "Gyroscope off");
    }
    if (in.restart) {
        if (s_mode == MODE_TEST)
            start_playtest();
        else
            restart_chamber();
    }

    hud_tick(dt);
    // The next chamber, once "Chamber complete" has been read: in play
    // only, never in the editor's play-test.
    if (s_mode != MODE_PLAY) s_pending_chamber = -1;
    if (s_pending_chamber >= 0 && !hud_message_up()) {
        play_chamber(s_pending_chamber);
        if (s_capturing) recording_capture_chamber(&s_cap, chamber_id(s_game.chamber));
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
    game_input_t step_in = gin;
    float        st      = dt;
    if (s_capturing && s_mode == MODE_PLAY) {
        // Recording: played on the frame's whole numbers, as a playback
        // of them will be.
        recording_frame_t const f = recording_frame(dt, &gin);
        st                        = recording_frame_input(&f, &step_in);
        if (!recording_capture_frame(&s_cap, &f)) record_stop();  // out of memory: what there is, kept
    }
    int const ev = game_step(&s_game, &step_in, st);
    sound_events(ev);
    if (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) render_set_level(&s_game.lv, s_game.portals);
    if (s_mode == MODE_TEST) {
        if (ev & PL_EV_DIED) {
            start_playtest();
            hud_message("Test subject lost. Again.");
        } else if ((ev & PL_EV_EXIT) && s_test_done <= 0.0f) {
            hud_message("It can be solved. Back to the editor...");
            s_test_done = HUD_MESSAGE_S;
        }
        return;
    }
    if (ev & PL_EV_DIED) {
        load_chamber(s_game.chamber);
        hud_message("Test subject lost. Again.");
        if (s_capturing) recording_capture_again(&s_cap);
    } else if (ev & PL_EV_EXIT) {
        int const next = s_game.chamber + 1;
        hud_message(next < level_count() ? "Chamber complete" : "All chambers complete. Cake later.");
        s_pending_chamber = next % level_count();
        // Continue: the next one, even if play stops before it loads.
        settings_set_chamber(chamber_id(s_pending_chamber));
        if (s_capturing) {
            recording_capture_done(&s_cap);
            if (next >= level_count()) record_stop();  // the last one: the run is done
        }
    }
}

static void on_update(float dt, void* user) {
    (void)user;
    if (dt > 0.0f) s_fps += (1.0f / dt - s_fps) * 0.1f;
    // The portals on LEDs A and B, while playing and if wanted.
    if (settings_leds() && s_mode != MODE_EDIT && !menu_on_title())
        leds_portals(s_game.portals[0].open, s_game.portals[1].open);
    else
        leds_release();
    sound_update();  // GLaDOS: her next sentence
    static float clock = 0.0f;
    clock              = fmodf(clock + dt, 3600.0f);  // the goo and fizzlers move by it
    render_set_time(clock);
    showtime_frame();
    devtest_update();
    if (s_demo >= 0) {
        demo_frame();
    } else if (watch_on()) {
        watch_update(dt);
    } else if (s_mode == MODE_EDIT) {
        edit_frame(dt);
    } else if (s_mode == MODE_TEST && (s_test_back || (s_test_done > 0.0f && (s_test_done -= dt) <= 0.0f))) {
        back_to_editor();
    } else if (menu_active()) {
        if (menu_on_title()) {
            hud_tick_message(dt);
            if (attract_update(&s_game, dt)) render_set_level(&s_game.lv, s_game.portals);
        }
        menu_frame();
    } else {
        play_frame(dt);
    }
}

static void on_backdrop(pax_buf_t* fb, void* user) {
    (void)fb;
    (void)user;  // every pixel is drawn by the passes; nothing to clear
}

static void on_render(pax_buf_t* fb, void* user) {
    (void)user;
    if (s_mode == MODE_EDIT) {
        editor_draw(fb);
        return;
    }
    bool const       half   = settings_half_res() && s_half_ok;
    bool const       title  = menu_on_title();
    pax_buf_t* const target = half ? &s_layer.buf : fb;
    scene_set_render_scale(half ? 2 : 1);

    int64_t const t0 = esp_timer_get_time();
    if (title) {
        // Through the attract mode's camera, the player's own view kept.
        float const yaw = s_game.pl.yaw, pitch = s_game.pl.pitch;
        attract_camera(&s_game, &s_game.pl.yaw, &s_game.pl.pitch);
        render_frame(target, &s_game);
        s_game.pl.yaw   = yaw;
        s_game.pl.pitch = pitch;
    } else {
        render_frame(target, &s_game);
    }
    if (title && attract_lit() < 1.0f) hud_fade(target, attract_lit());
    if (half) {
        // The CPU's pixels to PSRAM before the PPA's DMA reads them.
        se_ppa_layer_sync(&s_layer);
        if (se_ppa_blit_scaled(fb, 0, &s_layer, 2)) {
            se_ppa_wait_job(0);
            se_ppa_buf_invalidate(fb);  // the HUD draws on top with the CPU
        }
    }
    s_render_us = esp_timer_get_time() - t0;
    if (title) {
        hud_draw_title(fb, &s_game, menu_title_shown());
    } else {
        hud_timer_t      timer;
        hud_info_t const info = {
            .fps       = s_fps,
            .render_ms = (int)(s_render_us / 1000),
            .half      = half,
            .gyro      = settings_gyro(),
            .test      = s_demo >= 0,
            .recording = s_capturing,
            .timer     = watch_timer(&timer) ? &timer : NULL,
        };
        hud_draw(fb, &s_game, &info);
    }
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
        .f1_exits      = false,  // on_input: F1 saves a recording first
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
