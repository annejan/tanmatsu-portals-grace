// =====================================================================
//  Tanmatsu Portal -- a portal puzzle on SynthEngine3D
// =====================================================================

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp/device.h"
#include "chamber.h"
#include "demo.h"
#include "editor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "graceloader.h"
#include "input.h"
#include "leds.h"
#include "level.h"
#include "menu.h"
#include "nvs_settings_owner.h"
#include "pax_fonts.h"
#include "pax_gfx.h"
#include "pax_text.h"
#include "player.h"
#include "portal.h"
#include "recording.h"
#include "render.h"
#include "settings.h"
#include "sound.h"
#include "synthengine3d.h"
#include "testkit/devtest.h"
#include "testkit/showtime.h"

static char const TAG[] = "portal";

#define MESSAGE_S   2.5f
#define STORY_CPS   30.0f  // the story line types out this fast ...
#define STORY_HOLD  5.0f   // ... and stays this long once it is all there
#define CHAMBER_DIR "/sd/portals/chambers"

static game_t         s_game;  // the chamber in play: level, player, portals, cubes
static bool           s_half_ok;
static se_ppa_layer_t s_layer;

static char  s_msg[64];
static float s_msg_t;
static int   s_pending_chamber = -1;  // load this once the message is read

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

static void message(char const* text) {
    snprintf(s_msg, sizeof(s_msg), "%s", text);
    s_msg_t = MESSAGE_S;
}

// The chamber's story line: seconds since it started typing, and which
// chamber it was -- a restart does not tell it again.
static float s_story_t  = 0.0f;
static int   s_story_of = -2;

// What a turret said, as a subtitle, for this long.
#define TURRET_SUB_S 2.5f
static float       s_sub_t;
static int         s_sub_seen;
static char const* s_sub;

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
    if (fresh) s_story_t = 0.0f;
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
    message(s_game.lv.name);
    settings_set_chamber(chamber_id(index));
}

static bool same_portal(portal_t const* a, portal_t const* b) {
    return a->open == b->open && (!a->open || (a->face == b->face && memcmp(a->cell, b->cell, sizeof(a->cell)) == 0));
}

// --- Device tests (main/testkit) -------------------------------------------

static bool test_select(char const* name) {
    int const i = demo_find(name);
    if (i < 0) return false;
    menu_close();         // the title screen, at start-up: not in the test's shots ...
    s_game.chamber = -1;  // ... nor the mesh of the chamber playing behind it
    s_demo         = i;
    s_demo_t0      = showtime_now();
    s_msg_t        = 0.0f;
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

// --- Watching a recording ----------------------------------------------------
//
// Esc -> Watch a recording: a recording from the card (recording.h) played
// back chamber by chamber, each script stepped with each frame's own time,
// as a player at the keys would be, and timed by the game's clock: the run
// as this badge plays it -- the TAS's times, on the hardware. The times go
// to /sd/portals/<recording>-times.txt.

#define REC_HOLD_S 1.2f   // "Chamber complete", then the next
#define REC_GIVE_S 60.0f  // a run this long has lost its way

static bool          s_rec;                     // watching
static bool          s_rec_title;               // from the title screen, and back to it after
static char          s_rec_from[CHAMBER_ID_N];  // else the chamber play was in, and back to it
static int64_t       s_rec_left_us;             // when Esc stopped it: its twin is not a menu key
static recording_t   s_recording;
static char          s_rec_id[CHAMBER_ID_N];
static int           s_rec_k;  // the run
static demo_player_t s_rec_player;
static float         s_rec_run, s_rec_total, s_rec_hold;
static float         s_rec_times[RECORDING_MAX];  // each run's time, or -1: lost its way

static void rec_begin(int k) {
    s_rec_k     = k;
    s_rec_run   = 0.0f;
    s_rec_hold  = 0.0f;
    int const c = chamber_find(s_recording.runs[k].id);
    if (c < 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "No chamber %s", s_recording.runs[k].id);
        message(msg);
        s_rec_hold = REC_HOLD_S;
        return;
    }
    load_chamber(c);
    message(s_game.lv.name);
    demo_player_start(&s_rec_player, s_recording.runs[k].steps);
}

static bool rec_start(char const* id) {
    char err[96];
    if (!recording_load(RECORDING_DIR, id, &s_recording, err, sizeof(err))) {
        ESP_LOGW(TAG, "%s: %s", id, err);
        message(err);
        return false;
    }
    snprintf(s_rec_id, sizeof(s_rec_id), "%s", id);
    // From the start, as a new game: the music from its first bar, and the
    // first chamber's story told even if that chamber is the one in play.
    sound_restart_music();
    s_story_of  = -2;
    s_rec       = true;
    s_rec_total = 0.0f;
    for (int i = 0; i < RECORDING_MAX; i++) s_rec_times[i] = -1.0f;
    rec_begin(0);
    return true;
}

// The times so far, to the card: one line a chamber, then the total.
static void rec_write(int done) {
    char path[160];
    snprintf(path, sizeof(path), "/sd/portals/%s-times.txt", s_rec_id);
    FILE* f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "cannot write %s", path);
        return;
    }
    fprintf(f, "# %s\n", s_recording.name);
    for (int k = 0; k < done; k++) {
        if (s_rec_times[k] >= 0.0f)
            fprintf(f, "%s\t%.2f\n", s_recording.runs[k].id, (double)s_rec_times[k]);
        else
            fprintf(f, "%s\tFAIL\n", s_recording.runs[k].id);
    }
    fprintf(f, "total\t%.2f\t%.1f fps\n", (double)s_rec_total, (double)s_fps);
    fclose(f);
}

static void to_title(void);

// Out of a recording, stopped or done: to the title, or to the chamber
// play was in -- fresh, so play does not stand on the recording's exit,
// and saving nothing, so Continue stays where play was.
static void rec_leave(void) {
    s_rec = false;
    if (s_rec_title) {
        to_title();
    } else {
        int const at = chamber_find(s_rec_from);
        load_chamber(at >= 0 ? at : 0);
        message(s_game.lv.name);
    }
    input_resync();
}

static void rec_stop(void) {
    s_rec_left_us = esp_timer_get_time();
    rec_leave();
}

static void rec_update(float dt) {
    if (s_msg_t > 0.0f) s_msg_t -= dt;
    s_story_t += dt;
    if (s_sub_t > 0.0f) s_sub_t -= dt;
    char const* said = NULL;
    int const   n    = sound_turret_said(&said);
    if (n != s_sub_seen) {
        s_sub_seen = n;
        s_sub      = said;
        s_sub_t    = TURRET_SUB_S;
    }
    if (s_rec_hold > 0.0f) {
        if ((s_rec_hold -= dt) > 0.0f) return;
        if (s_rec_k + 1 < s_recording.n) {
            rec_begin(s_rec_k + 1);
        } else {
            rec_write(s_recording.n);
            char done[48];
            snprintf(done, sizeof(done), "Done: %d:%05.2f", (int)(s_rec_total / 60.0f),
                     (double)fmodf(s_rec_total, 60.0f));
            rec_leave();
            message(done);
        }
        return;
    }
    // The game's own clock: game_step() takes no step longer than 0.1 s.
    int const ev  = demo_player_step(&s_rec_player, &s_game, dt, 0.0f);
    s_rec_run    += fminf(dt, 0.1f);
    sound_events(ev);
    if (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) render_set_level(&s_game.lv, s_game.portals);
    if (ev & PL_EV_EXIT) {
        s_rec_times[s_rec_k]  = s_rec_run;
        s_rec_total          += s_rec_run;
        message("Chamber complete");
        s_rec_hold = REC_HOLD_S;
    } else if ((ev & PL_EV_DIED) || s_rec_run > REC_GIVE_S) {
        message("Lost its way");  // the frames came otherwise than the run was made for
        s_rec_hold = REC_HOLD_S * 2.0f;
    }
    if (s_rec_hold > 0.0f) rec_write(s_rec_k + 1);
}

// --- The title screen -------------------------------------------------------
//
// The game begins on the title screen (menu.c), and behind it the chambers
// play themselves: each one's solution, the player looking before each
// shot as a person would, one chamber after another in a shuffled order
// -- without the story, the sound effects or the HUD; the music plays on.

#define ATTRACT_PACE   0.35f  // s: the player looks this long before each shot and Use
#define ATTRACT_MAX_S  40.0f  // a run this long has gone on long enough
#define ATTRACT_DONE_S 2.5f   // the script has run out short of the exit: this long more
#define ATTRACT_END_S  1.2f   // through the exit: this long, fading, before the next
#define ATTRACT_FADE_S 0.6f

static demo_player_t s_att;
static int           s_att_order[CHAMBER_MAX];  // the chambers' solution demos, shuffled
static int           s_att_n = -1;              // how many; -1 until first counted
static int           s_att_k;                   // the one playing, in s_att_order
static float         s_att_t;                   // seconds into it
static float         s_att_end;                 // > 0: it is over, and the next comes in this long

static void attract_shuffle(void) {
    static uint32_t seed;
    if (seed == 0) seed = (uint32_t)esp_timer_get_time() | 1u;
    for (int i = s_att_n - 1; i > 0; i--) {
        seed           = seed * 1103515245u + 12345u;
        int const j    = (int)((seed >> 8) % (uint32_t)(i + 1));
        int const t    = s_att_order[i];
        s_att_order[i] = s_att_order[j];
        s_att_order[j] = t;
    }
}

static void attract_begin(void) {
    if (s_att_n < 0) {
        // The chambers with a solution: their demos are named after them.
        s_att_n = 0;
        for (int i = 0; i < demo_count() && s_att_n < CHAMBER_MAX; i++)
            if (strcmp(demo_name(i), chamber_id(demo_chamber(i))) == 0 && demo_has_solution(i))
                s_att_order[s_att_n++] = i;
        attract_shuffle();
        s_att_k = -1;
    }
    s_att_t   = 0.0f;
    s_att_end = 0.0f;
    if (s_att_n == 0) {  // none: the first chamber, standing still
        game_load(&s_game, 0);
        render_set_level(&s_game.lv, s_game.portals);
        static step_t const none[] = {{OP_END, 0, 0, 0, 0}};
        demo_player_start(&s_att, none);
        return;
    }
    if (++s_att_k >= s_att_n) {
        int const last = s_att_order[s_att_n - 1];
        attract_shuffle();
        if (s_att_n > 1 && s_att_order[0] == last) {  // not the same one twice running
            s_att_order[0]           = s_att_order[s_att_n - 1];
            s_att_order[s_att_n - 1] = last;
        }
        s_att_k = 0;
    }
    int const           i     = s_att_order[s_att_k];
    step_t const* const steps = i < demo_count() ? demo_steps(i) : NULL;
    if (steps == NULL) {  // the list changed under it: counted again
        s_att_n = -1;
        attract_begin();
        return;
    }
    game_load(&s_game, demo_chamber(i));
    render_set_level(&s_game.lv, s_game.portals);
    demo_player_start(&s_att, steps);
}

static void attract_update(float dt) {
    if (s_msg_t > 0.0f) s_msg_t -= dt;
    if (s_att_end > 0.0f && (s_att_end -= dt) <= 0.0f) {
        attract_begin();
        return;
    }
    int const ev  = demo_player_step(&s_att, &s_game, dt, ATTRACT_PACE);
    s_att_t      += dt;
    if (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) render_set_level(&s_game.lv, s_game.portals);
    if (s_att_end > 0.0f) return;
    if (ev & (PL_EV_EXIT | PL_EV_DIED))
        s_att_end = ATTRACT_END_S;
    else if (s_att_t > ATTRACT_MAX_S)
        s_att_end = ATTRACT_FADE_S;
    else if (demo_player_done(&s_att))
        s_att_end = ATTRACT_DONE_S;
}

// To the title screen, the chambers playing behind it.
static void to_title(void) {
    sound_hush();            // GLaDOS stops mid-sentence ...
    s_story_of        = -2;  // ... and tells the chamber's story again on the way back in
    s_pending_chamber = -1;
    s_sub_t           = 0.0f;
    s_sub_seen        = sound_turret_said(&s_sub);
    s_msg_t           = 0.0f;
    menu_title(chamber_find(settings_chamber()));
    attract_begin();
}

// How lit the chamber playing behind the title is: 0 black .. 1 full,
// fading in as it starts and out as it ends.
static float attract_lit(void) {
    float const in  = s_att_t / ATTRACT_FADE_S;
    float const out = s_att_end > 0.0f ? s_att_end / ATTRACT_FADE_S : 1.0f;
    return fmaxf(0.0f, fminf(1.0f, fminf(in, out)));
}

// Every pixel of an RGB565 frame scaled by `lit`, in place. PAX's
// translucent rectangle took some 70 ms a frame over the whole screen.
static void fade_565(pax_buf_t* buf, float lit) {
    uint16_t* const px   = pax_buf_get_pixels_rw(buf);
    size_t const    n    = pax_buf_get_size(buf) / 2;
    uint32_t const  k    = (uint32_t)(32.0f * lit);
    bool const      swap = buf->reverse_endianness;
    for (size_t i = 0; i < n; i++) {
        uint32_t p = px[i];
        if (swap) p = (p >> 8 | p << 8) & 0xFFFFu;
        p = ((p & 0xF81Fu) * k >> 5 & 0xF81Fu) | ((p & 0x07E0u) * k >> 5 & 0x07E0u);
        if (swap) p = (p >> 8 | p << 8) & 0xFFFFu;
        px[i] = (uint16_t)p;
    }
}

// The title's own few words: the chamber playing, and a message (a
// recording's time, say) along the bottom, under the title screen itself.
static void title_hud(pax_buf_t* fb) {
    rendertext_draw(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, 8, DISPLAY_LOG_H - 18, s_game.lv.name);
    if (s_msg_t > 0.0f && menu_title_shown()) {
        pax_vec2f const sz = rendertext_size(pax_font_sky_mono, 20, s_msg);
        rendertext_draw(fb, 0xFFFFFFFFu, pax_font_sky_mono, 20, RENDER_HALF_W - sz.x * 0.5f, DISPLAY_LOG_H - 44, s_msg);
    }
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
    to_title();
    devtest_start(&TEST);
}

static void start_playtest(void) {
    s_pending_chamber = -1;
    game_load_level(&s_game, editor_level());
    personalise(s_game.lv.story, sizeof(s_game.lv.story));
    s_story_t  = 0.0f;
    s_story_of = -1;
    sound_say(s_game.lv.story[0] ? s_game.lv.story : NULL);
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
        input_event(ev);
        return;
    }
    // Watching a recording, Esc stops it; nothing else plays.
    if (s_rec) {
        if (menu_is_open_key(ev)) rec_stop();
        return;
    }
    // A menu that is showing has the keyboard, all of it.
    if (menu_active()) {
        menu_event(ev);
    } else if (menu_is_open_key(ev)) {
        // The built-in keyboard's second Esc, after the one that stopped a
        // recording, does not open the menu as well.
        if (esp_timer_get_time() - s_rec_left_us < 250000) return;
        menu_open(s_game.chamber);
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
            s_pending_chamber = -1;
            load_chamber(s_game.chamber);
            message(s_game.lv.name);
            break;
        case MENU_CMD_CHAMBER:
            play_chamber(cmd.chamber);
            break;
        case MENU_CMD_NEW_GAME:
            // From the start: the music from its first bar, and the first
            // chamber's story told.
            sound_restart_music();
            s_story_of = -2;
            play_chamber(0);
            break;
        case MENU_CMD_EDITOR:
            // By name: saving in the editor re-reads the SD card, and the
            // list's order -- its indices -- may change.
            snprintf(s_play_id, sizeof(s_play_id), "%s", chamber_id(cmd.chamber));
            s_pending_chamber = -1;
            s_edit_title      = title;
            editor_open(cmd.chamber, CHAMBER_DIR);
            s_mode = MODE_EDIT;
            break;
        case MENU_CMD_WATCH:
            s_pending_chamber = -1;
            s_rec_title       = title;
            snprintf(s_rec_from, sizeof(s_rec_from), "%s", chamber_id(s_game.chamber));
            if (!rec_start(cmd.recording) && title) {
                char err[sizeof(s_msg)];
                snprintf(err, sizeof(err), "%s", s_msg);
                to_title();
                message(err);
            }
            break;
        case MENU_CMD_TITLE:
            to_title();
            break;
        case MENU_CMD_QUIT:
            sound_say(NULL);
            leds_release();          // the system LEDs back to the coprocessor
            audio_mixer_shutdown();  // se_audio.h: before the restart, or the speaker buzzes
            bsp_device_restart_to_launcher();
            break;
        default:
            break;
    }
    // Keys still held from the menu do not fire on the way out.
    if (!menu_active()) input_resync();
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
        return;
    }
    if (s_rec) {
        rec_update(dt);
        return;
    }
    if (s_mode == MODE_EDIT) {
        editor_cmd_t const c = editor_update(dt);
        if (c == EDITOR_CMD_PLAYTEST) start_playtest();
        if (c == EDITOR_CMD_QUIT) {
            s_mode  = MODE_PLAY;
            s_att_n = -1;  // saving re-read the card: the chambers, counted again
            if (s_edit_title) {
                to_title();
            } else {
                int const at = chamber_find(s_play_id);
                load_chamber(at >= 0 ? at : 0);
                message(s_game.lv.name);
            }
            input_resync();
        }
        return;
    }
    if (s_mode == MODE_TEST && (s_test_back || (s_test_done > 0.0f && (s_test_done -= dt) <= 0.0f))) {
        back_to_editor();
        return;
    }
    if (menu_active()) {
        if (menu_on_title()) attract_update(dt);
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
            s_pending_chamber = -1;
            load_chamber(s_game.chamber);
            message(s_game.lv.name);
        }
    }

    if (s_msg_t > 0.0f) s_msg_t -= dt;
    s_story_t += dt;
    if (s_sub_t > 0.0f) s_sub_t -= dt;
    char const* said = NULL;
    int const   n    = sound_turret_said(&said);
    if (n != s_sub_seen) {
        s_sub_seen = n;
        s_sub      = said;
        s_sub_t    = TURRET_SUB_S;
    }
    // The next chamber, once "Chamber complete" has been read: in play
    // only, never in the editor's play-test.
    if (s_mode != MODE_PLAY) s_pending_chamber = -1;
    if (s_pending_chamber >= 0 && s_msg_t <= 0.0f) {
        play_chamber(s_pending_chamber);
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
    if (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) render_set_level(&s_game.lv, s_game.portals);
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
        // Continue: the next one, even if play stops before it loads.
        settings_set_chamber(chamber_id(s_pending_chamber));
    }
}

static void on_backdrop(pax_buf_t* fb, void* user) {
    (void)fb;
    (void)user;  // every pixel is drawn by the passes; nothing to clear
}

static int  draw_story(pax_buf_t* fb);
static void draw_subtitle(pax_buf_t* fb, int above);

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

    rendertext_draw(fb, 0xFFFFFFFFu, pax_font_sky_mono, 16, 8, 6, s_game.lv.name);
    rendertext_draw(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, 8, 26, s_game.lv.hint);

    // Watching a recording: its timer, where the keys' help goes -- the run
    // so far, and this chamber's.
    if (s_rec) {
        float const total = s_rec_total + (s_rec_hold > 0.0f ? 0.0f : s_rec_run);
        char        big[24], small[80];
        snprintf(big, sizeof(big), "%d:%05.2f", (int)(total / 60.0f), (double)fmodf(total, 60.0f));
        snprintf(small, sizeof(small), "%s  %.2f", s_recording.name,
                 (double)(s_rec_hold > 0.0f && s_rec_times[s_rec_k] >= 0.0f ? s_rec_times[s_rec_k] : s_rec_run));
        pax_vec2f const bs = rendertext_size(pax_font_sky_mono, 24, big);
        pax_simple_rect(fb, 0xC0000000u, DISPLAY_LOG_W - bs.x - 20, 4, bs.x + 14, 46);
        rendertext_draw(fb, 0xFF78FF8Cu, pax_font_sky_mono, 24, DISPLAY_LOG_W - bs.x - 13, 6, big);
        pax_vec2f const ss = rendertext_size(pax_font_sky_mono, 12, small);
        rendertext_draw(fb, 0xFFC8C8C8u, pax_font_sky_mono, 12, DISPLAY_LOG_W - ss.x - 13, 34, small);
        draw_subtitle(fb, draw_story(fb));
        if (s_msg_t > 0.0f) {
            pax_vec2f const sz = rendertext_size(pax_font_sky_mono, 24, s_msg);
            rendertext_draw(fb, 0xFFFFFFFFu, pax_font_sky_mono, 24, cx - sz.x * 0.5f, cy - 70, s_msg);
        }
        return;
    }
    // The keys as they are bound now, not as they shipped.
    char blue[16], orange[16], use[16], help[112];
    snprintf(help, sizeof(help), "%s blue   %s orange   %s use   Esc menu",
             input_key_name(input_key(ACT_BLUE), blue, sizeof(blue)),
             input_key_name(input_key(ACT_ORANGE), orange, sizeof(orange)),
             input_key_name(input_key(ACT_USE), use, sizeof(use)));
    pax_vec2f const hs = rendertext_size(pax_font_sky_mono, 12, help);
    rendertext_draw(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, DISPLAY_LOG_W - 8 - hs.x, 6, help);

    if (s_demo >= 0) return;  // a device test's shots: nothing that varies run to run
    int  passes, tris;
    char stat[64];
    render_stats(&passes, &tris);
    snprintf(stat, sizeof(stat), "%2.0f fps %3lld ms  %d pass %d tri%s%s", s_fps, s_render_us / 1000, passes, tris,
             settings_half_res() && s_half_ok ? "  half" : "", settings_gyro() ? "  gyro" : "");
    rendertext_draw(fb, 0xFFA0A0A0u, pax_font_sky_mono, 12, 8, DISPLAY_LOG_H - 18, stat);
    draw_subtitle(fb, draw_story(fb));

    if (s_msg_t > 0.0f) {
        pax_vec2f const sz = rendertext_size(pax_font_sky_mono, 24, s_msg);
        rendertext_draw(fb, 0xFFFFFFFFu, pax_font_sky_mono, 24, cx - sz.x * 0.5f, cy - 70, s_msg);
    }
}

// The story line, typed out a letter at a time, in lines of whole words
// along the bottom of the screen. Returns how many lines it drew.
static int draw_story(pax_buf_t* fb) {
    char const* story = s_game.lv.story;
    int const   len   = (int)strlen(story);
    // Up while it types, a while after, and as long as GLaDOS is still saying it.
    if (len == 0 || (s_story_t > (float)len / STORY_CPS + STORY_HOLD && !sound_saying())) return 0;
    int shown = (int)(s_story_t * STORY_CPS);
    if (shown > len) shown = len;
    enum {
        WIDTH = 70,
        LINES = 3
    };
    char line[LINES][WIDTH + 1];
    int  n = 0, at = 0;
    while (at < shown && n < LINES) {
        int end = at + WIDTH < len ? at + WIDTH : len;
        if (end < len)  // break at the last space that fits
            for (int k = end; k > at; k--)
                if (story[k] == ' ') {
                    end = k;
                    break;
                }
        int const upto = end < shown ? end : shown;
        snprintf(line[n++], sizeof(line[0]), "%.*s", upto - at, story + at);
        at = end;
        while (at < len && story[at] == ' ') at++;
    }
    for (int i = 0; i < n; i++) {
        float const y = (float)(DISPLAY_LOG_H - 44 - (n - 1 - i) * 18);
        rendertext_draw(fb, 0xFFFFE08Au, pax_font_sky_mono, 14, 16, y, line[i]);
    }
    return n;
}

// A turret's last words, in its own colour, where the story line goes --
// above it, if `above` lines of it are up.
static void draw_subtitle(pax_buf_t* fb, int above) {
    if (s_sub_t <= 0.0f || s_sub == NULL) return;
    char text[48];
    snprintf(text, sizeof(text), "Turret: %s", s_sub);
    rendertext_draw(fb, 0xFFFF7A6Au, pax_font_sky_mono, 14, 16, (float)(DISPLAY_LOG_H - 44 - above * 18), text);
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
    if (menu_on_title() && attract_lit() < 1.0f) fade_565(target, attract_lit());
    if (half) {
        // The CPU's pixels to PSRAM before the PPA's DMA reads them.
        se_ppa_layer_sync(&s_layer);
        if (se_ppa_blit_scaled(fb, 0, &s_layer, 2)) {
            se_ppa_wait_job(0);
            se_ppa_buf_invalidate(fb);  // the HUD draws on top with the CPU
        }
    }
    s_render_us = esp_timer_get_time() - t0;
    if (menu_on_title())
        title_hud(fb);
    else
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
