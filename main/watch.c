#include "watch.h"
#include <math.h>
#include <stdio.h>
#include "app.h"
#include "chamber.h"
#include "demo.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "input.h"
#include "recording.h"
#include "render.h"
#include "sound.h"

static char const TAG[] = "watch";

#define REC_HOLD_S 1.2f   // "Chamber complete", then the next
#define REC_GIVE_S 60.0f  // a run this long has lost its way

static bool          s_on;
static bool          s_title;               // back to the title after
static char          s_back[CHAMBER_ID_N];  // else to this chamber
static int64_t       s_left_us;             // when Esc stopped it
static recording_t   s_recording;
static char          s_id[CHAMBER_ID_N];
static int           s_k;  // the run
static demo_player_t s_player;
static float         s_run, s_total, s_hold;
static float         s_times[RECORDING_MAX];  // each run's time, or -1: lost its way

bool watch_on(void) {
    return s_on;
}

static void begin(int k) {
    s_k         = k;
    s_run       = 0.0f;
    s_hold      = 0.0f;
    int const c = chamber_find(s_recording.runs[k].id);
    if (c < 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "No chamber %s", s_recording.runs[k].id);
        hud_message(msg);
        s_hold = REC_HOLD_S;
        return;
    }
    app_load_chamber(c);
    hud_message(app_game()->lv.name);
    demo_player_start(&s_player, s_recording.runs[k].steps);
}

bool watch_start(char const* id, bool to_title, char const* back) {
    char err[96];
    if (!recording_load(RECORDING_DIR, id, &s_recording, err, sizeof(err))) {
        ESP_LOGW(TAG, "%s: %s", id, err);
        hud_message(err);
        return false;
    }
    snprintf(s_id, sizeof(s_id), "%s", id);
    snprintf(s_back, sizeof(s_back), "%s", back);
    s_title = to_title;
    // From the start, as a new game: the music from its first bar, and the
    // first chamber's story told even if that chamber is the one in play.
    sound_restart_music();
    app_tell_again();
    s_on    = true;
    s_total = 0.0f;
    for (int i = 0; i < RECORDING_MAX; i++) s_times[i] = -1.0f;
    begin(0);
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
            fprintf(f, "%s\t%.2f\n", s_recording.runs[k].id, (double)s_times[k]);
        else
            fprintf(f, "%s\tFAIL\n", s_recording.runs[k].id);
    }
    fprintf(f, "total\t%.2f\t%.1f fps\n", (double)s_total, (double)app_fps());
    fclose(f);
}

// Out, stopped or done: to the title, or to the chamber play was in --
// fresh, so play does not stand on the recording's exit.
static void leave(void) {
    s_on = false;
    if (s_title) {
        app_to_title();
    } else {
        int const at = chamber_find(s_back);
        app_load_chamber(at >= 0 ? at : 0);
        hud_message(app_game()->lv.name);
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

void watch_update(float dt) {
    hud_tick(dt);
    if (s_hold > 0.0f) {
        if ((s_hold -= dt) > 0.0f) return;
        if (s_k + 1 < s_recording.n) {
            begin(s_k + 1);
        } else {
            write_times(s_recording.n);
            char done[48];
            snprintf(done, sizeof(done), "Done: %d:%05.2f", (int)(s_total / 60.0f), (double)fmodf(s_total, 60.0f));
            leave();
            hud_message(done);
        }
        return;
    }
    // The game's own clock: game_step() takes no step longer than 0.1 s.
    game_t* const g   = app_game();
    int const     ev  = demo_player_step(&s_player, g, dt, 0.0f);
    s_run            += fminf(dt, 0.1f);
    sound_events(ev);
    if (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) render_set_level(&g->lv, g->portals);
    if (ev & PL_EV_EXIT) {
        s_times[s_k]  = s_run;
        s_total      += s_run;
        hud_message("Chamber complete");
        s_hold = REC_HOLD_S;
    } else if ((ev & PL_EV_DIED) || s_run > REC_GIVE_S) {
        hud_message("Lost its way");  // the frames came otherwise than the run was made for
        s_hold = REC_HOLD_S * 2.0f;
    }
    if (s_hold > 0.0f) write_times(s_k + 1);
}

bool watch_timer(hud_timer_t* out) {
    if (!s_on) return false;
    *out = (hud_timer_t){
        .name  = s_recording.name,
        .total = s_total + (s_hold > 0.0f ? 0.0f : s_run),
        .run   = s_hold > 0.0f && s_times[s_k] >= 0.0f ? s_times[s_k] : s_run,
    };
    return true;
}
