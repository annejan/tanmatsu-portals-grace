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
#include "desk.h"
#include "deskview.h"
#include "editor.h"
#include "ghost.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lift.h"
#include "graceloader.h"
#include "hud.h"
#include "input.h"
#include "leds.h"
#include "level.h"
#include "menu.h"
#include "nvs_settings_owner.h"
#include "outro.h"
#include "pack.h"
#include "pax_gfx.h"
#include "player.h"
#include "portal.h"
#include "recording.h"
#include "render.h"
#include "review.h"
#include "settings.h"
#include "splash.h"
#include "sound.h"
#include "story.h"
#include "synthengine3d.h"
#include "testkit/devtest.h"
#include "testkit/showtime.h"
#include "watch.h"

#if __has_include("app_version.h")
#include "app_version.h"
#endif
#ifndef APP_VERSION
#define APP_VERSION ""
#endif
#ifndef APP_GIT_HASH
#define APP_GIT_HASH "unknown"
#endif

static char const TAG[] = "portal";

#define CHAMBER_DIR "/sd/portals/chambers"

static game_t         s_game;  // the chamber in play: level, player, portals, cubes
static bool           s_half_ok;
static se_ppa_layer_t s_layer;

static int   s_pending_chamber = -1;      // load this once the message is read
static int64_t s_loaded_us;               // when a chamber was last loaded: 0 once its first frame is played

// The lift between chambers (Settings: Lifts): up out of one, in the dark
// the next loaded, down into it -- no stop between them.
static lift_t       s_lift;
static lift_sites_t s_sites;  // the stations of the chamber in s_game (lift.h)
static uint32_t     s_sites_of;  // ... the load they are of (game_t.loaded)
static int    s_lift_next;
static bool   s_lift_hold;  // shut, until the message is read (a race's verdict, the end)

// The stations of the chamber in s_game, worked out once each time one is
// loaded -- by play, a play-test, Watch or the title's attract mode.
static lift_sites_t const* sites_now(void) {
    if (s_game.loaded != s_sites_of) {
        lift_sites(&s_game.lv, &s_sites);
        s_sites_of = s_game.loaded;
    }
    return &s_sites;
}

lift_sites_t const* app_lift_sites(void) {
    return sites_now();
}

// The lift's sounds.
static void lift_sounds(int ev) {
    if (ev & LIFT_EV_FIZZLE) sound_play(SND_FIZZLE);
    if (ev & LIFT_EV_DOOR) sound_play(SND_DOOR);
    if (ev & LIFT_EV_RIDE) sound_play(SND_TELEPORT);
    if (ev & LIFT_EV_LAND) sound_play(SND_LAND);
}

static int   s_story_end       = -1;      // a story pack done: its index, while its ending is told
static float s_story_end_t;               // ... for this long so far
static char  s_story_back[CHAMBER_ID_N];  // where Continue was before the story began: back to it after

// Playing the chambers, editing one, or play-testing the one being edited.
typedef enum {
    MODE_PLAY,
    MODE_EDIT,
    MODE_TEST,
    MODE_DESK,  // a desk story's desk (desk.h)
    MODE_CINE,   // its outro (outro.h)
    MODE_SPLASH  // the game's own splash, after the engine's (splash.h)
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
    char out[320];
    snprintf(out, sizeof(out), "%.*s%s%s", (int)(at - story), story, name, at + strlen(token));
    snprintf(story, n, "%.*s", (int)n - 1, out);  // fits: checked above
}

// --- A story told from a desk: its review rounds (story.h) -----------------
//
// Stories -> a desk story plays its rounds: each GLaDOS's draft of a
// chamber, patched with the flaws found so far. A death or a restart plays
// the same draft again; the exit judges the way there.

static story_t  s_story;
static bool     s_story_on;  // the chamber in play is s_story's round
static review_t s_review;    // its file's review keys
static draft_t* s_round_draft;
static char*    s_round_text;

static desk_data_t    s_desk_data;
static desk_t         s_desk;
static desk_action_t  s_desk_act;     // what a key at the desk asked for
static bool           s_desk_return;  // a round judged: back to the desk once GLaDOS is done
static story_result_t s_desk_result;  // ... with this
static bool           s_leaving;      // the day done: the ending said, then the title
static float          s_leaving_t;

#define SAVE_DIR "/sd/portals/saves"

// The round in play's chamber, in the chamber list.
static int round_chamber(void) {
    pack_t const* const p = pack_get(s_story.pack);
    return p != NULL && s_story.at < p->n ? p->chamber[s_story.at] : -1;
}

static void story_off(void) {
    s_story_on    = false;
    s_desk_return = false;
    s_leaving     = false;
    menu_set_round(false);
    if (s_mode == MODE_DESK || s_mode == MODE_CINE) s_mode = MODE_PLAY;
    free(s_round_draft);
    free(s_round_text);
    s_round_draft = NULL;
    s_round_text  = NULL;
}

// The round's draft into s_game: its file, patched. False, with a message,
// if it cannot be.
static bool round_load(int index) {
    char        err[96] = "";
    char const* text    = chamber_text(index);
    if (s_round_draft == NULL) s_round_draft = malloc(sizeof(*s_round_draft));
    if (s_round_text == NULL) s_round_text = malloc(CHAMBER_FILE_MAX);
    level_t* const lv = level_scratch();
    bool const     ok = text[0] != '\0' && s_round_draft != NULL && s_round_text != NULL &&
                        review_parse(text, &s_review, err, sizeof(err)) &&
                        story_round_text(&s_story, text, &s_review, s_round_draft, s_round_text, CHAMBER_FILE_MAX, err,
                                         sizeof(err)) >= 0 &&
                        chamber_parse(s_round_text, lv, NULL, NULL, err, sizeof(err));
    if (!ok) {
        char msg[HUD_MESSAGE_N];
        snprintf(msg, sizeof(msg), "Cannot read %.29s: %.20s", chamber_id(index), err[0] ? err : "no memory");
        ESP_LOGW(TAG, "round %s: %s", chamber_id(index), err);
        hud_message(msg);
        return false;
    }
    game_load_level(&s_game, lv);
    s_game.chamber = index;
    return true;
}

// --- Ghost races (ghost.h) ---------------------------------------------------

static ghost_t s_ghost;
static bool    s_ghost_live;  // this attempt is raced, and recorded for the next
static char    s_race_msg[HUD_MESSAGE_N];  // how the race went, at the exit

// Each racer's colour: yours cyan, the rivals' magenta, yellow and lime --
// their tints (render.h), and their names on the HUD.
static uint32_t const s_race_col[] = {0xFF3CE6FFu, 0xFFFF5ADCu, 0xFFFFDC3Cu, 0xFF78FF6Eu};

static int racer_tint(int i) {
    return s_ghost.mine ? i : i + 1;  // no best of yours: the rivals keep their colours
}

// The race on the HUD: this attempt's time, everyone's to beat, and each
// ghost's name over its head (after the frame is drawn: render_to_screen).
static bool race_info(hud_race_t* r) {
    if (!s_ghost_live || s_mode != MODE_PLAY) return false;
    memset(r, 0, sizeof(*r));
    r->now = s_ghost.now;
    for (int i = 0; i < ghost_count(&s_ghost) && r->n < HUD_RACERS; i++) {
        ghost_racer_t const* g = &s_ghost.racer[i];
        snprintf(r->line[r->n].name, sizeof(r->line[0].name), "%s", s_ghost.mine && i == 0 ? "best" : g->name);
        r->line[r->n].time = g->best_s;
        r->line[r->n].col  = s_race_col[racer_tint(i) % 4];
        r->n++;
        player_t p;
        float    x, y;
        // Its name, if it is in sight: drawn (not one the eye is in, as
        // render.c leaves out), and not behind a wall.
        if (!ghost_pose(&s_ghost, i, &p)) continue;
        vec3_t const eye = player_eye(&s_game.pl), at = player_eye(&p);
        vec3_t const d   = v3_sub(at, eye);
        float const  len = v3_len(d);
        if (len <= 0.6f || level_raycast(&s_game.lv, eye, v3_scale(d, 1.0f / len), len).hit) continue;
        if (render_to_screen(v3(p.pos.x, p.pos.y + 2.05f, p.pos.z), &x, &y)) {
            snprintf(r->tag[r->n_tags].name, sizeof(r->tag[0].name), "%s", s_ghost.mine && i == 0 ? "you" : g->name);
            r->tag[r->n_tags].x   = x;
            r->tag[r->n_tags].y   = y;
            r->tag[r->n_tags].col = s_race_col[racer_tint(i) % 4];
            r->n_tags++;
        }
    }
    return true;
}

// No more racing: out of play, into a story, a recording, the editor.
static void race_stop(void) {
    if (s_ghost_live) ghost_end(&s_ghost);
    s_ghost_live = false;
}

static bool ghosts_wanted(void) {
    return settings_ghosts() && s_mode == MODE_PLAY && !s_story_on && !watch_on() && s_demo < 0;
}

static bool load_chamber(int index) {
    bool const round = s_story_on && index == round_chamber();
    if (round ? !round_load(index) : !game_load(&s_game, index)) {
        // A file on the card that does not read: the chamber in play stays.
        if (!round) {
            char msg[HUD_MESSAGE_N];
            snprintf(msg, sizeof(msg), "Cannot read %.40s", chamber_id(index));
            hud_message(msg);
        }
        return false;
    }
    bool const fresh = index != s_story_of;  // a restart does not tell it again
    if (fresh) hud_story_start();
    s_story_of = index;
    personalise(s_game.lv.story, sizeof(s_game.lv.story));
    if (fresh) sound_say(s_game.lv.story[0] ? s_game.lv.story : NULL);
    render_set_level(&s_game.lv, s_game.portals);
    ESP_LOGI(TAG, "chamber %d: %s", index, s_game.lv.name);
    // A new attempt -- the chamber's start, a restart, after a death: raced
    // against its best, if there is one.
    s_ghost_live = ghosts_wanted();
    if (s_ghost_live) ghost_begin(&s_ghost, GHOST_DIR, chamber_id(index), &s_game.lv, GAME_PHYSICS);
    s_loaded_us = esp_timer_get_time();
    return true;
}

// Play moves on to chamber `index`: where Continue, on the title screen,
// comes back to. False, with a message, if it cannot be read.
static bool play_chamber(int index) {
    s_pending_chamber = -1;
    s_lift.phase      = LIFT_NONE;  // the lift's own next sets it going down again
    s_story_end       = -1;
    if (s_story_on && index != round_chamber()) story_off();  // play has gone elsewhere
    if (!load_chamber(index)) return false;                   // it said why
    if (s_story_on) {
        // The round, not the chamber: Continue stays where it was.
        char msg[HUD_MESSAGE_N];
        if (s_desk_data.badge)
            snprintf(msg, sizeof(msg), "%.60s", s_game.lv.name);  // a chore, not a round
        else if (s_story.drafts > 1)
            snprintf(msg, sizeof(msg), "Round %d: %.30s, draft %d", s_story.round, s_game.lv.name, s_story.drafts);
        else
            snprintf(msg, sizeof(msg), "Round %d: %.40s", s_story.round, s_game.lv.name);
        hud_message(msg);
        return true;
    }
    hud_message(s_game.lv.name);
    settings_set_chamber(chamber_id(index));
    return true;
}

// The chamber after `c` in play order: its story pack's next (-1 after its
// last), else the next played in order, round to the first.
static int next_after(int c) {
    int       at;
    int const pk = pack_of(c, &at);
    if (pk >= 0) return at + 1 < pack_get(pk)->n ? pack_get(pk)->chamber[at + 1] : -1;
    return (c + 1) % chamber_main_n();
}

// Play from chamber `c` -- or, if it cannot be read, from the next that
// can, in play order. False if none can.
static bool play_from(int c) {
    for (int tries = 0; c >= 0 && tries < CHAMBER_MAX; tries++, c = next_after(c))
        if (play_chamber(c)) return true;
    return false;
}

static void to_title(void);

// Play could not go where it was going: to the title screen, saying why.
static void title_saying_why(void) {
    char why[HUD_MESSAGE_N];
    snprintf(why, sizeof(why), "%s", hud_message_text());
    to_title();
    hud_message(why);
}

// --- Recording a run ------------------------------------------------------
//
// Esc -> Record from here (or Record a run, on the title): what you press,
// frame by frame, from the chamber's start, through every chamber you get
// through, until Stop recording or until play goes elsewhere. A death or
// a restart starts the chamber's recording over, so a run keeps only the
// clean ones. Saved to RECORDING_DIR/run-NN.txt, for Watch a recording.



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
static bool record_start(int index) {
    if (index < 0) {
        sound_restart_music();
        s_story_of = -2;
        index      = 0;
    }
    if (!play_chamber(index)) return false;
    recording_capture_start(&s_cap);
    recording_capture_chamber(&s_cap, chamber_id(index));
    s_capturing = true;
    menu_set_recording(true);
    hud_message("Recording");
    return true;
}

// The chamber in play, again from its start.
static void restart_chamber(void) {
    s_pending_chamber = -1;
    s_lift.phase      = LIFT_NONE;
    s_story_end       = -1;
    if (!load_chamber(s_game.chamber)) {  // gone from the card since
        title_saying_why();
        return;
    }
    hud_message(s_game.lv.name);
    // Its recording from the start again -- or, restarted after its exit,
    // recorded once more.
    if (s_capturing) recording_capture_chamber(&s_cap, chamber_id(s_game.chamber));
}

// To the title screen, the chambers playing behind it.
static void to_title(void) {
    record_stop();
    race_stop();
    s_lift.phase = LIFT_NONE;
    story_off();
    s_story_end = -1;
    sound_hush();            // GLaDOS stops mid-sentence ...
    s_story_of        = -2;  // ... and tells the chamber's story again on the way back in
    s_pending_chamber = -1;
    hud_quiet();
    // Continue: a chamber, or a desk story ("desk:<pack>").
    int story = -1;
    if (strncmp(settings_chamber(), "desk:", 5) == 0)
        for (int i = 0; i < pack_count(); i++)
            if (pack_get(i)->desk && strcmp(pack_get(i)->id, settings_chamber() + 5) == 0) story = i;
    if (story >= 0)
        menu_title_story(story);
    else
        menu_title(chamber_find(settings_chamber()));
    attract_begin(&s_game);
    render_set_level(&s_game.lv, s_game.portals);
}

// --- For watch.c (app.h) ------------------------------------------------------

game_t* app_game(void) {
    return &s_game;
}
bool app_load_chamber(int index) {
    return load_chamber(index);
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

// How much memory is left once everything is up, to the card: what the
// chamber size limits (level.h) can grow into. The P4's console is out of
// reach without the C6's tty; badgelink fetches this.
static void write_memory(void) {
    FILE* f = fopen("/sd/portals/memory.txt", "w");
    if (f == NULL) return;
    fprintf(f, "build\t%s %s\n", APP_VERSION, APP_GIT_HASH);
    fprintf(f, "chamber max\t%d x %d x %d cells, level_t %u bytes, game_t %u bytes\n", LV_MAX_W, LV_MAX_H, LV_MAX_D,
            (unsigned)sizeof(level_t), (unsigned)sizeof(game_t));
    fprintf(f, "psram free\t%u\tlargest block\t%u\tlowest free\t%u\n",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
            (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    fprintf(f, "internal free\t%u\tlargest block\t%u\tlowest free\t%u\n",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    // What meshing a chamber takes here (render.c meshes only what a portal
    // shot changes; a whole chamber when one loads): the one behind the title.
    int64_t const t0 = esp_timer_get_time();
    int const     m  = level_mesh(&s_game.lv, NULL, 0, NULL, 0);
    fprintf(f, "mesh %s\t%d x %d x %d\t%d quads\t%lld us\n", chamber_id(s_game.chamber), s_game.lv.w, s_game.lv.h,
            s_game.lv.d, m, (long long)(esp_timer_get_time() - t0));
    fclose(f);
}

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
    // The story packs on the card, after them (pack.h).
    int const packs = pack_load(PACK_DIR);
    if (packs > 0) ESP_LOGI(TAG, "%d story pack(s) from %s", packs, PACK_DIR);

    // The half-size layer a quarter-resolution frame draws into.
    s_half_ok = false;
    if (se_ppa_init()) {
        se_display_info_t di;
        se_display_info(&di);
        s_half_ok = se_ppa_layer_alloc(&s_layer, DISPLAY_LOG_W / 2, DISPLAY_LOG_H / 2, di.pax_format, di.reversed,
                                       di.orientation);
    }
    if (!s_half_ok) ESP_LOGW(TAG, "no quarter-resolution layer; drawing at full resolution");

    // The engine's own splash, as games show what they are built on; then
    // ours, drawn by the game (splash.h), and the title screen after it.
    se_splash();
    sound_init();
    sound_set_music(settings_music());
    sound_set_effects(settings_effects());
    sound_set_voice(settings_voice());
    attract_seed((uint32_t)esp_timer_get_time());
    if (splash_start(&s_game)) {
        s_mode = MODE_SPLASH;
        render_set_level(&s_game.lv, s_game.portals);
    } else {
        to_title();
    }
    devtest_start(&TEST);
    write_memory();
}

static void start_playtest(void) {
    s_pending_chamber = -1;
    s_lift.phase      = LIFT_NONE;
    game_load_level(&s_game, editor_level());
    personalise(s_game.lv.story, sizeof(s_game.lv.story));
    hud_story_start();
    s_story_of = -1;
    sound_say(s_game.lv.story[0] ? s_game.lv.story : NULL);
    render_set_level(&s_game.lv, s_game.portals);
    s_loaded_us = esp_timer_get_time();
    s_mode      = MODE_TEST;
    s_test_back = false;
    s_test_done = 0.0f;
    hud_message(editor_debugging() ? "Reach the exit: repaired. Esc: the debugger"
                                   : "Play-test: Esc goes back to the editor");
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
    if (s_mode == MODE_DESK) {
        desk_action_t const a = deskview_event(&s_desk, ev);
        if (a != DESK_NONE) s_desk_act = a;
        return;
    }
    if (s_mode == MODE_SPLASH) {
        // Any key: on to the title.
        if ((ev->type == INPUT_EVENT_TYPE_NAVIGATION && ev->args_navigation.state) ||
            ev->type == INPUT_EVENT_TYPE_KEYBOARD)
            splash_skip();
        return;
    }
    if (s_mode == MODE_CINE) {
        // Esc, Enter or Space: on.
        if (ev->type == INPUT_EVENT_TYPE_NAVIGATION && ev->args_navigation.state &&
            (ev->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_ESC ||
             ev->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_RETURN ||
             ev->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_SPACE_L ||
             ev->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_SPACE_M ||
             ev->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_SPACE_R))
            outro_skip();
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
        // Not in a lift: two seconds, and it is somewhere else by then.
        // Not while riding (a few seconds, and somewhere else by then) --
        // but in a pack's last lift, shut while its ending is told, yes.
        if (!watch_just_stopped() && (!lift_on(&s_lift) || s_lift.stay)) menu_open(s_game.chamber);
    } else {
        input_event(ev);
    }
}

// --- The desk ---------------------------------------------------------------

static void save_path(char* out, size_t n) {
    snprintf(out, n, "%s/%.40s.txt", SAVE_DIR, pack_get(s_story.pack)->id);
}

// The story where it stands, on the card: Continue comes back to it.
static void desk_save(void) {
    char text[512], path[96];
    if (desk_save_text(&s_desk, text, sizeof(text)) < 0) return;
    mkdir("/sd/portals", 0755);
    mkdir(SAVE_DIR, 0755);
    save_path(path, sizeof(path));
    FILE* f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "cannot save %s", path);
        return;
    }
    bool const ok = fputs(text, f) >= 0;
    if (fclose(f) != 0 || !ok) ESP_LOGW(TAG, "saving %s failed", path);
}

// To desk story `pk`'s desk: where its save left it, or its day's start.
static bool desk_enter(int pk) {
    pack_t const* const p = pack_get(pk);
    char                dir[160], err[96];
    snprintf(dir, sizeof(dir), "%s/%.40s", PACK_DIR, p->id);
    if (!desk_load(&s_desk_data, dir, err, sizeof(err))) {
        ESP_LOGW(TAG, "%s: %s", dir, err);
        char msg[HUD_MESSAGE_N];
        snprintf(msg, sizeof(msg), "%.60s", err);
        hud_message(msg);
        return false;
    }
    story_off();
    race_stop();
    story_begin(&s_story, pk);
    // A save, if there is one.
    char     path[96];
    uint32_t read  = 0;
    bool     saved = false;
    save_path(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (f != NULL) {
        char         text[512];
        size_t const n = fread(text, 1, sizeof(text) - 1, f);
        fclose(f);
        text[n] = '\0';
        saved   = desk_load_save(text, &s_story, &s_desk_data, &read);
        if (!saved) ESP_LOGW(TAG, "%s does not read: the day from its start", path);
    }
    desk_begin(&s_desk, &s_desk_data, &s_story, p->n, true);
    // Its rounds' names, for TODO.
    static char names[PACK_CHAMBERS][32];
    for (int i = 0; i < p->n; i++) chamber_name(p->chamber[i], names[i], sizeof(names[i]));
    desk_set_rounds(&s_desk, p, names);
    if (saved) {
        s_desk.read = read;
        desk_print(&s_desk, "Session restored.");
    }
    s_story_on = true;
    s_desk_act = DESK_NONE;
    s_mode     = MODE_DESK;
    hud_quiet();
    char id[CHAMBER_ID_N];
    snprintf(id, sizeof(id), "desk:%.40s", p->id);
    settings_set_chamber(id);
    return true;
}

// A frame at the desk: what its keys asked for.
static void desk_frame(float dt) {
    if (s_leaving) {
        // The ending said, a moment more, then the title screen.
        s_leaving_t += dt;
        if (s_leaving_t > 4.0f && !sound_saying()) {
            if (s_story_back[0]) settings_set_chamber(s_story_back);
            to_title();
        }
        return;
    }
    desk_action_t const act = s_desk_act;
    s_desk_act              = DESK_NONE;
    switch (act) {
        case DESK_PLAY: {
            // A broken draft: into the debugger, to repair it.
            int const   c       = round_chamber();
            char const* text    = chamber_text(c);
            char        err[96] = "";
            if (review_parse(text, &s_review, err, sizeof(err)) && s_review.kind == REVIEW_BROKEN) {
                if (editor_open_debug(text, chamber_id(c), &s_review, err, sizeof(err))) {
                    desk_print(&s_desk, "GLaDOS cannot solve this draft. It is broken. Opening the debugger...");
                    s_mode = MODE_EDIT;
                } else {
                    desk_print(&s_desk, err);
                }
                input_resync();
                break;
            }
            s_mode = MODE_PLAY;
            menu_set_round(true);
            if (!play_chamber(round_chamber())) {
                s_mode = MODE_DESK;
                menu_set_round(false);
                desk_print(&s_desk, hud_message_text());
            }
            input_resync();
            break;
        }
        case DESK_TITLE:
            desk_save();
            to_title();
            break;
        case DESK_LEAVE: {
            // The day is done: its save gone, so the story starts afresh;
            // the ending said -- over the outro, if there is one, else at
            // the desk.
            char path[96];
            save_path(path, sizeof(path));
            chamber_remove_file(path);
            char ending[sizeof(s_game.lv.story)];
            snprintf(ending, sizeof(ending), "%s", pack_get(s_story.pack)->ending);
            personalise(ending, sizeof(ending));
            int const outro = pack_get(s_story.pack)->outro_chamber;
            if (outro >= 0 && outro_start(&s_game, outro, &s_desk_data)) {
                s_mode = MODE_CINE;
                snprintf(s_game.lv.story, sizeof(s_game.lv.story), "%s", ending);
                hud_quiet();
                hud_story_start();
                sound_say(ending[0] ? ending : NULL);
                render_set_level(&s_game.lv, s_game.portals);
                break;
            }
            desk_print(&s_desk, "");
            desk_print(&s_desk, ending);
            sound_say(ending[0] ? ending : NULL);
            s_leaving   = true;
            s_leaving_t = 0.0f;
            break;
        }
        default:
            break;
    }
}

// A frame of the outro; the title screen once it is over.
static void cine_frame(float dt) {
    int ev = 0;
    hud_tick(dt);
    if (!outro_update(&s_game, dt, &ev)) {
        if (s_story_back[0]) settings_set_chamber(s_story_back);
        to_title();
        return;
    }
    sound_events(ev);
    if (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) render_set_level(&s_game.lv, s_game.portals);
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
            if (!play_chamber(cmd.chamber) && title) title_saying_why();
            break;
        case MENU_CMD_RECORD:
            record_stop();
            if (!record_start(cmd.chamber) && title) title_saying_why();
            break;
        case MENU_CMD_RECORD_STOP:
            record_stop();
            break;
        case MENU_CMD_DESK:
            // Back to the desk from a round: the draft as it was.
            sound_hush();
            s_pending_chamber = -1;
            s_desk_return     = false;
            menu_set_round(false);
            s_mode = MODE_DESK;
            break;
        case MENU_CMD_STORY: {
            // A story pack from its start: the music from its first bar.
            pack_t const* const p = pack_get(cmd.chamber);
            if (p == NULL) break;
            record_stop();
            if (p->desk) {
                snprintf(s_story_back, sizeof(s_story_back), "%s",
                         strncmp(settings_chamber(), "desk:", 5) == 0 ? "" : settings_chamber());
                if (!desk_enter(cmd.chamber) && title) title_saying_why();
                break;
            }
            snprintf(s_story_back, sizeof(s_story_back), "%s", settings_chamber());
            sound_restart_music();
            s_story_of = -2;
            if (!play_from(p->chamber[0]) && title) title_saying_why();
            break;
        }
        case MENU_CMD_NEW_GAME:
            record_stop();
            // From the start: the music from its first bar, and the first
            // chamber's story told.
            sound_restart_music();
            s_story_of = -2;
            if (!play_chamber(0) && title) title_saying_why();
            break;
        case MENU_CMD_EDITOR:
            // By name: saving in the editor re-reads the SD card, and the
            // list's order -- its indices -- may change.
            record_stop();
            snprintf(s_play_id, sizeof(s_play_id), "%s", chamber_id(cmd.chamber));
            s_pending_chamber = -1;
            s_edit_title      = title;
            race_stop();
            editor_open(cmd.chamber, CHAMBER_DIR);
            s_mode = MODE_EDIT;
            break;
        case MENU_CMD_WATCH:
            record_stop();
            race_stop();
            s_pending_chamber = -1;
            if (!watch_start(cmd.recording_dir, cmd.recording, cmd.pack, title, chamber_id(s_game.chamber)) && title) {
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
    if (editor_debugging()) {
        // Out of the debugger, unrepaired: back to the desk.
        s_mode = MODE_DESK;
        desk_print(&s_desk, "Debugger closed. The draft is still broken.");
        input_resync();
        return;
    }
    s_mode = MODE_PLAY;
    pack_load(PACK_DIR);  // saving re-read the card, the packs' chambers dropped: read again
    attract_recount();    // and the chambers counted again
    if (s_edit_title) {
        to_title();
    } else {
        int const at = chamber_find(s_play_id);
        if (load_chamber(at >= 0 ? at : 0))
            hud_message(s_game.lv.name);
        else
            title_saying_why();
    }
    input_resync();
}

// A pack's end: its ending, in the story line's place, said and typed out
// (so it is read with the voice off too); and Continue back where it was
// before the story began.
static void story_ending(int pk) {
    char msg[HUD_MESSAGE_N];
    snprintf(msg, sizeof(msg), "%.40s: the end", pack_get(pk)->name);
    hud_message(msg);
    snprintf(s_game.lv.story, sizeof(s_game.lv.story), "%s", pack_get(pk)->ending);
    personalise(s_game.lv.story, sizeof(s_game.lv.story));
    hud_story_start();
    sound_say(s_game.lv.story[0] ? s_game.lv.story : NULL);
    if (s_story_back[0]) settings_set_chamber(s_story_back);
    s_story_end   = pk;
    s_story_end_t = 0.0f;
}

// A round's exit: judged, GLaDOS's verdict said and typed out, and the
// next round waiting until she is done.
static void round_exit(void) {
    pack_t const* const  p   = pack_get(s_story.pack);
    story_result_t const res = story_exit(&s_story, &s_review, &s_game.lv, &s_game.track, (story_rounds_t){p->n, p->route_of});
    char                 terms[160];
    review_describe(&s_game.lv, &s_game.track, terms, sizeof(terms));
    ESP_LOGI(TAG, "round %d: %s (%s); score %d", s_story.round, res.headline, terms, s_story.score);
    hud_message(res.headline);
    snprintf(s_game.lv.story, sizeof(s_game.lv.story), "%s", res.line);
    hud_story_start();
    sound_say(s_game.lv.story);
    // Back to the desk once she is done; the next file's chamber tells its
    // own story, the same one's does not.
    s_desk_return = true;
    s_desk_result = res;
    if (res.outcome == STORY_NEXT || res.outcome == STORY_DONE) s_story_of = -2;
}

// A frame of play, or of a play-test.
static void play_frame(float dt) {
    input_frame_t in;
    input_poll(&in, dt, settings_gyro());

    if (in.gyro) {
        settings_set_gyro(!settings_gyro());
        hud_message(settings_gyro() ? "Gyroscope on" : "Gyroscope off");
    }
    if (in.restart && (!lift_on(&s_lift) || s_lift.stay)) {  // not riding: it is on its way already
        if (s_mode == MODE_TEST)
            start_playtest();
        else
            restart_chamber();
    }

    hud_tick(dt);
    // The next chamber, once "Chamber complete" has been read: in play
    // only, never in the editor's play-test.
    if (s_mode != MODE_PLAY) {
        s_pending_chamber = -1;
        s_lift.phase      = LIFT_NONE;
    }
    if (lift_on(&s_lift)) {
        // The lift: in, shut, up, and down into the next. Jump, Use or a
        // shot hurries it.
        bool const  hurry = in.jump || in.use || in.fire[0] || in.fire[1];
        int const   ev    = lift_step(&s_lift, &s_game, dt, s_lift_hold && hud_message_up(), in.dyaw, in.dpitch,
                                      hurry ? 3.0f : 1.0f);
        lift_sounds(ev);
        if (ev & LIFT_EV_LANDED) input_resync();  // there: on with it
        if (ev & LIFT_EV_TOP) {
            // At the top, in the dark: the next chamber, and down into it.
            if (!play_from(s_lift_next)) {
                s_lift.phase = LIFT_NONE;
                title_saying_why();
                return;
            }
            if (s_capturing) recording_capture_chamber(&s_cap, chamber_id(s_game.chamber));
            lift_down(&s_lift, &s_game, &sites_now()->start);
            if (!lift_on(&s_lift)) input_resync();  // no start station: there at once, the hurrying key let go
            return;
        }
        if (s_story_end < 0) return;  // a pack's end goes on, shut in the lift
    }
    // A story pack's end: its ending told -- typed out, and said -- then the
    // title screen.
    if (s_story_end >= 0) {
        s_story_end_t    += dt;
        float const told  = (float)strlen(s_game.lv.story) / 30.0f + 3.0f;
        if (s_story_end_t > fmaxf(told, HUD_MESSAGE_S) && !sound_saying()) {
            s_story_end = -1;
            to_title();
        }
        return;
    }
    if (s_desk_return) {
        // A round judged: back to the desk once GLaDOS has had her say.
        if (hud_message_up() || sound_saying()) return;
        s_desk_return = false;
        s_mode        = MODE_DESK;
        menu_set_round(false);
        desk_after_round(&s_desk, &s_desk_result);
        desk_save();
        return;
    }
    if (s_pending_chamber >= 0 && !hud_message_up()) {
        // The next -- or, if it cannot be read, the next that can.
        if (!play_from(s_pending_chamber))
            title_saying_why();
        else if (s_capturing)
            recording_capture_chamber(&s_cap, chamber_id(s_game.chamber));
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
    recording_frame_t f = {0};
    if (s_mode == MODE_PLAY) {
        // Played on the frame's whole numbers, as a playback of them -- a
        // recording, a ghost -- will be.
        f  = recording_frame(dt, &gin);
        st = recording_frame_input(&f, &step_in);
        if (s_capturing && !recording_capture_frame(&s_cap, &f)) record_stop();  // out of memory: what there is, kept
    }
    int const ev = game_step(&s_game, &step_in, st);
    if (s_ghost_live && s_mode == MODE_PLAY) ghost_frame(&s_ghost, &f);
    sound_events(ev);
    if (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) render_set_level(&s_game.lv, s_game.portals);
    if (s_mode == MODE_TEST) {
        if (ev & PL_EV_DIED) {
            start_playtest();
            hud_message("Test subject lost. Again.");
        } else if ((ev & PL_EV_EXIT) && s_story_on && editor_debugging()) {
            // The repair works: the round is done.
            round_exit();
        } else if ((ev & PL_EV_EXIT) && s_test_done <= 0.0f) {
            hud_message("It can be solved. Back to the editor...");
            s_test_done = HUD_MESSAGE_S;
        }
        return;
    }
    if (ev & PL_EV_DIED) {
        if (!load_chamber(s_game.chamber)) {
            title_saying_why();
            return;
        }
        hud_message("Test subject lost. Again.");
        if (s_capturing) recording_capture_again(&s_cap);
    } else if ((ev & PL_EV_EXIT) && s_story_on) {
        round_exit();
    } else if (ev & PL_EV_EXIT) {
        // The next: in a story pack, its next; else the next played in
        // order, round to the first after the last.
        int        at;
        int const  pk      = pack_of(s_game.chamber, &at);
        int const  inorder = chamber_main_n();
        bool const last    = pk >= 0 ? at + 1 >= pack_get(pk)->n : s_game.chamber + 1 >= inorder;
        // Raced: the time, against the best; a better one saved.
        s_race_msg[0] = '\0';
        if (s_ghost_live) {
            mkdir("/sd/portals", 0755);
            mkdir(GHOST_DIR, 0755);
            char nick[32] = "";
            if (nvs_settings_get_owner_nickname(nick, sizeof(nick), "") != ESP_OK) nick[0] = '\0';
            ghost_result_t const gr = ghost_finish(&s_ghost, GHOST_DIR, nick);
            if (gr.of > 1 + (gr.before >= 0.0f ? 1 : 0)) {
                // Rivals raced: your place among them.
                static char const* const th[] = {"th", "st", "nd", "rd"};
                snprintf(s_race_msg, sizeof(s_race_msg), "%.2f s%s -- %d%s of %d", (double)gr.time,
                         gr.best ? ", a new best" : "", gr.place, th[gr.place < 4 ? gr.place : 0], gr.of);
            } else if (gr.best && gr.before >= 0.0f)
                snprintf(s_race_msg, sizeof(s_race_msg), "%.2f s -- a new best! (-%.2f)", (double)gr.time,
                         (double)(gr.before - gr.time));
            else if (gr.best)
                snprintf(s_race_msg, sizeof(s_race_msg), "%.2f s -- your best, for now", (double)gr.time);
            else if (gr.faster && gr.lost)
                snprintf(s_race_msg, sizeof(s_race_msg), "%.2f s (no memory: not saved)", (double)gr.time);
            else if (gr.faster) {
                // Not saved, twice tried: where it stopped, the card's errno
                // (5 EIO, 12 ENOMEM, 23 ENFILE, 28 ENOSPC), and where FatFs's
                // sector buffer went (LP or TCM: the card cannot DMA from it).
                static char const* const step[] = {"?", "nothing", "open", "write", "close"};
                int const                st      = gr.err >= 0 && gr.err <= 4 ? gr.err : 0;
                uintptr_t const          a       = recording_write_probe;
                char const* const ram = a >= 0x4ff00000u && a < 0x4ffc0000u   ? "L2"
                                        : a >= 0x48000000u && a < 0x4c000000u ? "PS"
                                        : a >= 0x50108000u && a < 0x50110000u ? "LP"
                                        : a >= 0x30100000u && a < 0x30102000u ? "TCM"
                                                                              : "?";
                snprintf(s_race_msg, sizeof(s_race_msg), "%.2f s not saved: %s e%d %s d%uK", (double)gr.time, step[st],
                         recording_write_errno, ram,
                         (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024));
                ESP_LOGW(TAG, "ghost not saved: %s", s_race_msg);
            }
            else
                snprintf(s_race_msg, sizeof(s_race_msg), "%.2f s (+%.2f on your best)", (double)gr.time,
                         (double)(gr.time - gr.before));
            s_ghost_live = false;
        }
        if (pk >= 0 && last) {
            story_ending(pk);
            // Into the lift, shut, while its ending is told.
            lift_site_t site;
            if (settings_lifts() && !s_story_on && lift_exit_for(&s_game.lv, sites_now(), s_game.pl.pos, &site))
                lift_enter(&s_lift, &s_game, &site, true);
            if (s_race_msg[0]) {
                // The pack's end, and how the last race went.
                char msg[HUD_MESSAGE_N];
                snprintf(msg, sizeof(msg), "%.18s: the end. %.32s", pack_get(pk)->name, s_race_msg);
                hud_message(msg);
            }
        } else {
            char done[HUD_MESSAGE_N];
            if (last && s_race_msg[0])
                snprintf(done, sizeof(done), "All done! %.53s", s_race_msg);
            else
                snprintf(done, sizeof(done), "%s",
                         s_race_msg[0] ? s_race_msg : last ? "All chambers complete. Cake later." : "Chamber complete");
            hud_message(done);
            int const next = pk >= 0 ? pack_get(pk)->chamber[at + 1] : (s_game.chamber + 1) % inorder;
            // Continue: the next one, even if play stops before it loads.
            settings_set_chamber(chamber_id(next));
            lift_site_t site;
            if (settings_lifts() && lift_exit_for(&s_game.lv, sites_now(), s_game.pl.pos, &site)) {
                // Into the lift, and up and out into the next: no stop.
                lift_enter(&s_lift, &s_game, &site, false);
                s_lift_next = next;
                s_lift_hold = s_race_msg[0] || last;
            } else {
                s_pending_chamber = next;
            }
        }
        if (s_capturing) {
            recording_capture_done(&s_cap);
            if (last) record_stop();  // the last one: the run is done
        }
    }
}

static void on_update(float dt, void* user) {
    (void)user;
    if (s_loaded_us != 0) {
        // The first frame in a chamber: from when it was there, not from
        // the frame before, which loaded it. That frame's length would be
        // the game's first step -- up to 0.1 s gone before the player can
        // move, in a recording, a ghost and a TAS (watch.c) too.
        float const since = (float)(esp_timer_get_time() - s_loaded_us) * 1e-6f;
        s_loaded_us       = 0;
        if (since > 0.0f && since < dt) dt = since;
    }
    if (dt > 0.0f) s_fps += (1.0f / dt - s_fps) * 0.1f;
    // The portals on LEDs A and B, while playing and if wanted.
    if (settings_leds() && s_mode != MODE_EDIT && s_mode != MODE_DESK && s_mode != MODE_CINE && !menu_on_title())
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
    } else if (s_mode == MODE_DESK) {
        desk_frame(dt);
    } else if (s_mode == MODE_CINE) {
        cine_frame(dt);
    } else if (s_mode == MODE_SPLASH) {
        if (!splash_update(&s_game, dt)) {
            s_mode = MODE_PLAY;
            render_set_portal_depth(settings_portal_depth());  // the player's own, again
            to_title();
        }
    } else if (s_mode == MODE_TEST && (s_test_back || (s_test_done > 0.0f && (s_test_done -= dt) <= 0.0f))) {
        back_to_editor();
    } else if (menu_active()) {
        if (menu_on_title()) {
            hud_tick_message(dt);
            if (attract_update(&s_game, dt)) render_set_level(&s_game.lv, s_game.portals);
        }
        menu_frame();
        if (s_ghost_live && !settings_ghosts()) race_stop();  // turned off in Settings
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
    if (s_mode == MODE_DESK) {
        deskview_draw(fb, &s_desk);
        return;
    }
    if (s_mode == MODE_CINE && !outro_world()) {
        outro_draw(fb);
        return;
    }
    bool const       half   = settings_half_res() && s_half_ok;
    bool const       title  = menu_on_title();
    pax_buf_t* const target = half ? &s_layer.buf : fb;
    scene_set_render_scale(half ? 2 : 1);

    // The ghosts being raced, where they are.
    player_t gp[GHOST_RIVALS + 1];
    uint8_t  gt[GHOST_RIVALS + 1];
    int      ng = 0;
    if (s_ghost_live && s_mode == MODE_PLAY && !title && !lift_on(&s_lift))
        for (int i = 0; i < ghost_count(&s_ghost); i++)
            if (ghost_pose(&s_ghost, i, &gp[ng])) gt[ng++] = (uint8_t)racer_tint(i);
    render_set_ghosts(gp, gt, ng);
    // The lift's tube, from where the player's feet were to above where
    // they go.
    // The lift stations, and the one being ridden: in play, in a
    // play-test and behind the title -- not in a desk story's rounds.
    lift_t const* const lift    = watch_on() ? watch_lift() : &s_lift;
    bool const          lifting = lift != NULL && lift_on(lift) && s_mode == MODE_PLAY && !title;
    if (settings_lifts() && !s_story_on && (s_mode == MODE_PLAY || s_mode == MODE_TEST)) {
        lift_view_t view;
        lift_view(lifting ? lift : NULL, sites_now(), &view);
        render_set_lift(&view);
    } else {
        render_set_lift(NULL);
    }
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
    if (s_mode == MODE_CINE) hud_fade(target, outro_lit());  // the lights low, then out
    if (s_mode == MODE_SPLASH && splash_lit() < 1.0f) hud_fade(target, splash_lit());  // faded in, and out
    // The lift: dark at the top of the shaft, up out of one chamber and
    // down into the next.
    if (lifting) hud_fade(target, lift_lit(lift));
    if (half) {
        // The CPU's pixels to PSRAM before the PPA's DMA reads them.
        se_ppa_layer_sync(&s_layer);
        if (se_ppa_blit_scaled(fb, 0, &s_layer, 2)) {
            se_ppa_wait_job(0);
            se_ppa_buf_invalidate(fb);  // the HUD draws on top with the CPU
        }
    }
    s_render_us = esp_timer_get_time() - t0;
    if (s_mode == MODE_SPLASH) {
        splash_draw(fb);  // the title, over the corridor
    } else if (title) {
        hud_draw_title(fb, &s_game, menu_title_shown());
    } else {
        hud_timer_t      timer;
        hud_race_t       race;
        hud_info_t const info = {
            .fps       = s_fps,
            .render_ms = (int)(s_render_us / 1000),
            .half      = half,
            .gyro      = settings_gyro(),
            .test      = s_demo >= 0,
            .recording = s_capturing,
            .timer     = watch_timer(&timer) ? &timer : NULL,
            .race      = race_info(&race) ? &race : NULL,
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
