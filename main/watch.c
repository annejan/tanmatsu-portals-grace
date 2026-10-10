#include "watch.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "chamber.h"
#include "demo.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lift.h"
#include "input.h"
#include "menu.h"
#include "recording.h"
#include "render.h"
#include "settings.h"
#include "sound.h"

static char const TAG[] = "watch";

#if __has_include("app_version.h")
#include "app_version.h"
#endif
#ifndef APP_VERSION
#define APP_VERSION ""
#endif
#ifndef APP_GIT_HASH
#define APP_GIT_HASH "unknown"
#endif

// Whether a recording made on `version` (release, then build) was made on
// this build -- or, if it gives only a release, on this release.
static bool same_version(char const* version) {
    if (version[0] == '\0' || APP_VERSION[0] == '\0') return true;
    if (strchr(version, ' ') == NULL) return strcmp(version, APP_VERSION) == 0;
    return strcmp(version, APP_VERSION " " APP_GIT_HASH) == 0;
}

#define REC_HOLD_S 1.2f   // "Chamber complete", then the next
#define REC_GIVE_S 60.0f  // a run this long has lost its way

static bool          s_on;
static bool          s_title;               // back to the title after
static char          s_back[CHAMBER_ID_N];  // else to this chamber
static int64_t       s_left_us;             // when Esc stopped it
static recording_t   s_recording;
static char          s_id[CHAMBER_ID_N];
static char          s_pack[32];  // a story pack's replay: its pack, for its chambers' short names
static int           s_k;         // the run
static demo_player_t s_player;
static float         s_run, s_total, s_hold;
static float         s_times[RECORDING_MAX];  // each run's time, or -1: lost its way
// A recorded run's frames, played at their own times: the next, and the
// time come that is not yet played.
static int           s_fi;
static float         s_acc;
// The badge's own frames while each run played (Settings -> Frame times).
static int           s_frames[RECORDING_MAX];
static float         s_frame_s[RECORDING_MAX], s_worst[RECORDING_MAX];
// Between runs, with Settings -> Lifts: up out of one, down into the next.
static lift_t        s_lift;

bool watch_on(void) {
    return s_on;
}

lift_t const* watch_lift(void) {
    return s_on ? &s_lift : NULL;
}

static void begin(int k) {
    s_k          = k;
    s_run        = 0.0f;
    s_hold       = 0.0f;
    s_fi         = 0;
    s_acc        = 0.0f;
    s_frames[k]  = 0;
    s_frame_s[k] = s_worst[k] = 0.0f;
    int c                     = chamber_find(s_recording.runs[k].id);
    if (c < 0 && s_pack[0]) {  // a pack's replay, naming its chamber by the file alone
        char full[96];
        snprintf(full, sizeof(full), "%.31s/%.63s", s_pack, s_recording.runs[k].id);
        c = chamber_find(full);
    }
    if (c < 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "No chamber %s", s_recording.runs[k].id);
        hud_message(msg);
        s_hold = REC_HOLD_S;
        return;
    }
    if (!app_load_chamber(c)) {  // on the card, but it does not read: lost, said so
        s_hold = REC_HOLD_S;
        return;
    }
    hud_message(app_game()->lv.name);
    demo_player_start(&s_player, s_recording.runs[k].steps);
}

bool watch_start(char const* dir, char const* id, char const* pack, bool to_title, char const* back) {
    char err[96];
    if (!recording_load(dir, id, &s_recording, err, sizeof(err))) {
        ESP_LOGW(TAG, "%s: %s", id, err);
        hud_message(err);
        return false;
    }
    // Its times file: a pack's replay as <pack>-<id>.
    if (pack[0])
        snprintf(s_id, sizeof(s_id), "%.24s-%.36s", pack, id);
    else
        snprintf(s_id, sizeof(s_id), "%s", id);
    snprintf(s_pack, sizeof(s_pack), "%s", pack);
    snprintf(s_back, sizeof(s_back), "%s", back);
    s_title = to_title;
    // From the start, as a new game: the music from its first bar, and the
    // first chamber's story told even if that chamber is the one in play.
    sound_restart_music();
    app_tell_again();
    s_on         = true;
    s_total      = 0.0f;
    s_lift.phase = LIFT_NONE;
    for (int i = 0; i < RECORDING_MAX; i++) s_times[i] = -1.0f;
    begin(0);
    // Recorded on another version, the same frames may go otherwise.
    if (!same_version(s_recording.version)) {
        char msg[HUD_MESSAGE_N];
        snprintf(msg, sizeof(msg), "Recorded on another build: %.36s", s_recording.version);
        hud_message(msg);
    }
    return true;
}

// The times so far, to the card: one line a chamber, then the total.
static void write_times(int done) {
    char path[160];
    snprintf(path, sizeof(path), "/sd/portals/%s-times.txt", s_id);
    FILE* f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "cannot write %s", path);
        return;
    }
    fprintf(f, "# %s\n", s_recording.name);
    for (int k = 0; k < done; k++) {
        if (s_times[k] >= 0.0f)
            fprintf(f, "%s\t%.2f", s_recording.runs[k].id, (double)s_times[k]);
        else
            fprintf(f, "%s\tFAIL", s_recording.runs[k].id);
        // With Settings -> Frame times: the badge's frame rate in it, and its slowest frame.
        if (settings_frame_times() && s_frames[k] > 0)
            fprintf(f, "\t%.1f fps\t%.0f ms", (double)((float)s_frames[k] / s_frame_s[k]),
                    (double)(s_worst[k] * 1000.0f));
        fprintf(f, "\n");
    }
    fprintf(f, "total\t%.2f\t%.1f fps\n", (double)s_total, (double)app_fps());
    fclose(f);
}

// The times, on screen: each chamber's and the total, and with Settings ->
// Frame times the badge's frame rate in each and its slowest frame.
static void summary(void) {
    static char        value[RECORDING_MAX][40];
    static char const* ids[RECORDING_MAX];
    static char const* values[RECORDING_MAX];
    for (int k = 0; k < s_recording.n; k++) {
        int const at = s_times[k] >= 0.0f ? snprintf(value[k], sizeof(value[k]), "%.2f", (double)s_times[k])
                                          : snprintf(value[k], sizeof(value[k]), "lost");
        if (settings_frame_times() && s_frames[k] > 0 && at > 0)
            snprintf(value[k] + at, sizeof(value[k]) - (size_t)at, " %.0ffps %.0fms",
                     (double)((float)s_frames[k] / s_frame_s[k]), (double)(s_worst[k] * 1000.0f));
        ids[k]    = s_recording.runs[k].id;
        values[k] = value[k];
    }
    char total[24];
    snprintf(total, sizeof(total), "%d:%05.2f", (int)(s_total / 60.0f), (double)fmodf(s_total, 60.0f));
    char         name[RECORDING_NAME_N + 40];
    size_t const release = strcspn(s_recording.version, " ");  // the release only: the build is in the file
    if (s_recording.version[0])
        snprintf(name, sizeof(name), "%s (version %.*s)", s_recording.name, (int)(release < 16 ? release : 16),
                 s_recording.version);
    else
        snprintf(name, sizeof(name), "%s", s_recording.name);
    menu_times(name, s_recording.n, ids, values, total);
}

// Out, stopped or done: to the title, or to the chamber play was in --
// fresh, so play does not stand on the recording's exit.
static void leave(void) {
    s_on         = false;
    s_lift.phase = LIFT_NONE;
    if (s_title) {
        app_to_title();
    } else {
        int const at = chamber_find(s_back);
        if (app_load_chamber(at >= 0 ? at : 0))
            hud_message(app_game()->lv.name);
        else
            app_to_title();
    }
    input_resync();
}

void watch_stop(void) {
    s_left_us = esp_timer_get_time();
    leave();
}

bool watch_just_stopped(void) {
    return esp_timer_get_time() - s_left_us < 250000;
}

// The next run, or, after the last, the times.
static void next_run(void) {
    if (s_k + 1 < s_recording.n) {
        begin(s_k + 1);
    } else {
        write_times(s_recording.n);
        char done[48];
        snprintf(done, sizeof(done), "Done: %d:%05.2f", (int)(s_total / 60.0f), (double)fmodf(s_total, 60.0f));
        leave();
        hud_message(done);
        summary();
    }
}

void watch_update(float dt) {
    hud_tick(dt);
    if (lift_on(&s_lift)) {
        // Between runs, in the lift: no run's clock goes meanwhile.
        game_t* const g  = app_game();
        int const     ev = lift_step(&s_lift, g, dt, false, 0.0f, 0.0f, 1.5f);
        if (ev & LIFT_EV_FIZZLE) sound_play(SND_FIZZLE);
        if (ev & LIFT_EV_DOOR) sound_play(SND_DOOR);
        if (ev & LIFT_EV_RIDE) sound_play(SND_TELEPORT);
        if (ev & LIFT_EV_LAND) sound_play(SND_LAND);
        if (ev & LIFT_EV_TOP) {
            s_lift.phase = LIFT_NONE;
            next_run();
            // It would not load: wait on the car's floor, not up the shaft.
            if (s_on && s_hold > 0.0f) g->pl.pos = v3(s_lift.car.x, s_lift.car.y, s_lift.car.z);
            // Loaded: down into it (not after the last, nor if it would not load).
            if (s_on && s_hold <= 0.0f) {
                lift_down(&s_lift, g, &app_lift_sites()->start);
                if (lift_on(&s_lift)) sound_play(SND_TELEPORT);
            }
        }
        return;
    }
    if (s_hold > 0.0f) {
        if ((s_hold -= dt) > 0.0f) return;
        next_run();
        return;
    }
    // The badge's frames, but not the one that loaded the chamber.
    if (s_run > 0.0f || s_fi > 0) {
        s_frames[s_k]++;
        s_frame_s[s_k] += dt;
        if (dt > s_worst[s_k]) s_worst[s_k] = dt;
    }
    game_t* const                g   = app_game();
    recording_run_t const* const run = &s_recording.runs[s_k];
    int                          ev  = 0;
    bool                         out = false;  // a recorded run whose frames ran out short of the exit
    if (run->frames > 0) {
        // Recorded: each frame stepped with its own time and input, as
        // it was played, however this badge's frames come.
        s_acc += dt;
        for (int n = 0; n < 8 && s_fi < run->frames; n++) {
            recording_frame_t const* const f  = &s_recording.frame[run->frame0 + s_fi];
            float const                    fd = (float)f->dt_us * 1e-6f;
            if (fd > s_acc) break;
            game_input_t in;
            recording_frame_input(f, &in);
            ev    |= game_step(g, &in, fd);
            s_acc -= fd;
            s_run += fminf(fd, 0.1f);
            s_fi++;
        }
        if (s_acc > 1.0f) s_acc = 1.0f;  // a slow badge plays it slower, not in jumps (a frame is at most 1 s)
        out = s_fi >= run->frames && !(ev & PL_EV_EXIT);
    } else {
        // Scripted: stepped with each of this badge's frames. The game's
        // own clock: game_step() takes no step longer than 0.1 s.
        ev     = demo_player_step(&s_player, g, dt, 0.0f);
        s_run += fminf(dt, 0.1f);
    }
    sound_events(ev);
    if (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) render_set_level(&g->lv, g->portals);
    if (ev & PL_EV_EXIT) {
        s_times[s_k]  = s_run;
        s_total      += s_run;
        hud_message("Chamber complete");
        lift_site_t site;
        if (settings_lifts() && lift_exit_for(&g->lv, app_lift_sites(), g->pl.pos, &site)) {
            // Into the lift, up and out, into the next, as play goes (main.c).
            lift_enter(&s_lift, g, &site, false);
            write_times(s_k + 1);
            return;
        }
        s_hold = REC_HOLD_S;
    } else if ((ev & PL_EV_DIED) || out || (run->frames == 0 && s_run > REC_GIVE_S)) {
        hud_message("Lost its way");  // the frames came otherwise than the run was made for
        s_hold = REC_HOLD_S * 2.0f;
    }
    if (s_hold > 0.0f) write_times(s_k + 1);
}

bool watch_timer(hud_timer_t* out) {
    if (!s_on) return false;
    bool const between = s_hold > 0.0f || lift_on(&s_lift);  // the run done, in s_total already
    *out               = (hud_timer_t){
                      .name  = s_recording.name,
                      .total = s_total + (between ? 0.0f : s_run),
                      .run   = between && s_times[s_k] >= 0.0f ? s_times[s_k] : s_run,
    };
    return true;
}
