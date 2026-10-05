// Host movie: demos' runs, as frames and sound, for tools/make_movie.py.
//
//   host_movie SECONDS DEMO [DEMO...]
//
// films the demos one after the other, as the game goes from chamber to
// chamber, each at most SECONDS long: it writes $BUILD/shots/movie_N.ppm
// at 10 frames a second, $BUILD/movie.wav and $BUILD/movie.txt (below).
// The frames come from host_shot's rasterizer, included below, through a
// camera of their own (below); the sound is the game's own -- sound.c,
// speech.c with SAM, the engine's music -- mixed here offline as the
// badge's mixer mixes it, on one clock, so the music plays on from
// chamber to chamber.

#define main host_shot_main
#include "host_shot.c"
#undef main

#include "sound.h"

#define FPS        10
#define TICK       (1.0f / 50.0f)
#define STORY_CPS  30.0f  // as main.c types the story line ...
#define STORY_HOLD 5.0f   // ... and holds it
#define TURRET_SUB 2.5f   // ... and a turret's words
#define MESSAGE_S  2.5f   // ... and "Chamber complete": then the next chamber
#define OPEN_S     4.0f   // the opening and closing titles
#define CLOSE_S    4.5f
#define MAX_TICKS  (200 * 50)

// --- The mixer --------------------------------------------------------------

#define SLOTS 8
static sfx_voice_t*    s_voices[SLOTS];
static music_source_t* s_music;
static bool            s_music_on = true, s_group_on[SE_AUDIO_SFX_GROUP_COUNT] = {true, true, true, true};
static int             s_music_vol = 100;

esp_err_t audio_mixer_init(void) {
    return ESP_OK;
}
void audio_mixer_set_music(music_source_t* src) {
    s_music = src;
}
bool audio_mixer_register_voice(sfx_voice_t* v) {
    for (int i = 0; i < SLOTS; i++)
        if (s_voices[i] == NULL) {
            v->finished = false;
            s_voices[i] = v;
            return true;
        }
    return false;
}
void audio_mixer_stop_voice(sfx_voice_t* v) {
    v->finished = true;
}
void audio_mixer_set_music_enabled(bool on) {
    s_music_on = on;
}
void audio_mixer_set_group_enabled(uint8_t group, bool on) {
    if (group < SE_AUDIO_SFX_GROUP_COUNT) s_group_on[group] = on;
}
void audio_mixer_set_music_volume(uint8_t percent) {
    s_music_vol = percent;
}

// `frames` stereo frames, as the mixer sums them: music at its gain and
// volume, each effect at the effects' gain, clipped.
static void mix(int16_t* out, size_t frames) {
    static int16_t one[2 * 1024];
    static int32_t acc[2 * 1024];
    memset(acc, 0, sizeof(acc));
    if (s_music_on && s_music != NULL) {
        memset(one, 0, sizeof(one));
        s_music->render(s_music, one, frames);
        for (size_t j = 0; j < 2 * frames; j++) acc[j] += (int32_t)(one[j] * AUDIO_MUSIC_GAIN * s_music_vol / 100);
    }
    for (int i = 0; i < SLOTS; i++) {
        sfx_voice_t* v = s_voices[i];
        if (v == NULL) continue;
        if (!v->finished && (v->group >= SE_AUDIO_SFX_GROUP_COUNT || s_group_on[v->group])) {
            memset(one, 0, sizeof(one));
            v->render(v, one, frames);
            for (size_t j = 0; j < 2 * frames; j++) acc[j] += (int32_t)(one[j] * AUDIO_SFX_GAIN);
        }
        if (v->finished) {
            if (v->shutdown) v->shutdown(v);
            s_voices[i] = NULL;
        }
    }
    for (size_t j = 0; j < 2 * frames; j++)
        out[j] = (int16_t)(acc[j] > 32767 ? 32767 : acc[j] < -32768 ? -32768 : acc[j]);
}

// --- The camera ---------------------------------------------------------------
//
// A script turns the player in an instant, and fires the same step: on
// film the portal would appear before the view swung to it. So each run is
// played twice. The first time logs the view at every step; from that the
// camera is worked out ahead: it is on the aim a moment before every shot,
// pickup, put-down and press, it looks where it walks rather than at the
// floor, it eases into every turn from both sides, and it sways a little,
// as a head does. Through a portal it cuts, as the view does.

#define AIM_BEFORE 0.6f   // s on the aim before the act
#define AIM_AFTER  0.25f  // ... and after it
#define EASE_S     0.16f  // the turns' easing: a Gaussian this wide
#define WALK_PITCH 0.10f  // walking, it looks this far down (rad)
#define PACE_S     0.8f   // the player stands this long before each act, looking

typedef struct {
    float yaw, pitch;
    float vx, vy, vz;
    bool  ground;
    int   ev;
} view_t;

static view_t s_log[MAX_TICKS];
static float  s_cam[MAX_TICKS][2];
static int    s_n;

typedef struct {
    bool exited;
} plan_t;

static void log_tick(game_t const* g, int ev, float now, void* ctx) {
    (void)now;
    if (ev & PL_EV_EXIT) ((plan_t*)ctx)->exited = true;
    if (s_n >= MAX_TICKS) return;
    s_log[s_n++] = (view_t){g->pl.yaw, g->pl.pitch, g->pl.vel.x, g->pl.vel.y, g->pl.vel.z, g->pl.on_ground, ev};
}

static void plan_camera(void) {
    int const acts =
        GAME_EV_SHOT_BLUE | GAME_EV_SHOT_ORANGE | GAME_EV_SHOT_FAIL | GAME_EV_PICKUP | GAME_EV_DROP | GAME_EV_PRESS;
    int const    before = (int)(AIM_BEFORE / TICK), after = (int)(AIM_AFTER / TICK);
    static float want[MAX_TICKS][2];
    static bool  aimed[MAX_TICKS];
    memset(aimed, 0, sizeof(aimed));
    for (int k = 0; k < s_n; k++) {
        view_t const* v = &s_log[k];
        float const   h = sqrtf(v->vx * v->vx + v->vz * v->vz);
        want[k][0]      = v->yaw;
        want[k][1]      = v->pitch;
        if (v->ground) {
            // Walking: where it goes, not at its feet.
            if (h > 1.5f) {
                want[k][0] = atan2f(v->vx, v->vz);
                want[k][1] = WALK_PITCH;
            }
        } else if (h > 1.0f || fabsf(v->vy) > 1.5f) {
            // Carried, flung or falling: the way it goes, up or down --
            // not backwards down a funnel, facing where the script last
            // pointed.
            if (h > 1.0f) want[k][0] = atan2f(v->vx, v->vz);
            if (h <= 1.0f && k > 0) want[k][0] = want[k - 1][0];
            float p    = -atanf(v->vy / fmaxf(h, 0.5f)) * 0.6f;
            want[k][1] = p < -0.6f ? -0.6f : p > 0.9f ? 0.9f : p;
        } else if (k > 0) {
            // Hanging still in the air -- at a funnel's end: the same way,
            // but looking down, for where to get off.
            want[k][0] = want[k - 1][0];
            want[k][1] = 0.7f;
        }
    }
    // On the aim before each act, and a little after: the act's own view.
    for (int k = 0; k < s_n; k++) {
        if (!(s_log[k].ev & acts)) continue;
        for (int j = k - before; j <= k + after && j < s_n; j++) {
            if (j < 0) continue;
            // Not back across a portal: the view before it is another room.
            bool crossed = false;
            for (int m = j < k ? j + 1 : k + 1; m <= (j < k ? k : j); m++) crossed |= (s_log[m].ev & PL_EV_TELEPORT);
            if (crossed) continue;
            want[j][0] = s_log[k].yaw;
            want[j][1] = s_log[k].pitch;
            aimed[j]   = true;
        }
    }
    // Eased, each stretch between portals on its own; yaw unwrapped first.
    int const sigma = (int)(EASE_S / TICK), reach = 3 * sigma;
    for (int a = 0; a < s_n;) {
        int b = a + 1;
        while (b < s_n && !(s_log[b].ev & PL_EV_TELEPORT)) b++;
        for (int k = a + 1; k < b; k++) {
            float d = want[k][0] - want[k - 1][0];
            while (d > 3.14159265f) d -= 6.2831853f;
            while (d < -3.14159265f) d += 6.2831853f;
            want[k][0] = want[k - 1][0] + d;
        }
        for (int k = a; k < b; k++)
            for (int c = 0; c < 2; c++) {
                float sum = 0.0f, wsum = 0.0f;
                for (int j = k - reach; j <= k + reach; j++) {
                    if (j < a || j >= b) continue;
                    float const w  = expf(-0.5f * (float)((j - k) * (j - k)) / (float)(sigma * sigma));
                    sum           += want[j][c] * w;
                    wsum          += w;
                }
                s_cam[k][c] = sum / wsum;
                // On the aim at the act itself, exactly: the portal goes
                // where the crosshair is.
                if (aimed[k] && (s_log[k].ev & acts)) s_cam[k][c] = want[k][c];
            }
        a = b;
    }
    // A head is never quite still.
    for (int k = 0; k < s_n; k++) {
        float const t  = (float)k * TICK;
        s_cam[k][0]   += 0.010f * sinf(0.63f * t) + 0.005f * sinf(1.71f * t + 1.0f);
        s_cam[k][1]   += 0.007f * sinf(0.89f * t + 2.0f);
    }
}

// --- The recording ------------------------------------------------------------
//
// $BUILD/movie.txt has a line per frame of film -- "C" the opening titles,
// "P\tN\ttyped\tturret\tmessage" the picture movie_N with the HUD on it,
// "E" the closing titles -- and "S\tname\thint\tstory" where a chamber
// begins.

typedef struct {
    FILE*       wav;
    FILE*       txt;
    long        frames_out;  // audio frames written
    int         frame;       // frames of film, titles included
    int         pic;         // pictures written
    int         step;        // steps into this run: its camera
    float       t0;          // the film's clock when this run began
    float       last;        // the run's clock at its last step recorded
    char const* story;
    int         sub_seen;
    char const* sub;
    float       sub_t;
    float       exit_t;  // when the player reached the exit, or -1
    char const* done;    // what the game says at the exit
} rec_t;

// The sound, up to the film's time `t`.
static void sound_to(rec_t* r, float t) {
    long const want = (long)(t * (float)AUDIO_SAMPLE_RATE_HZ + 0.5f);
    while (r->frames_out < want) {
        int16_t buf[2 * 1024];
        size_t  n = (size_t)(want - r->frames_out);
        if (n > 1024) n = 1024;
        mix(buf, n);
        fwrite(buf, sizeof(int16_t), 2 * n, r->wav);
        r->frames_out += (long)n;
    }
}

// Titles for `seconds`: frames with no picture, and the sound going on.
static void titles(rec_t* r, char const* line, float seconds) {
    for (float t = 0.0f; t < seconds - 1e-4f; t += TICK) {
        sound_update();
        sound_to(r, r->t0 + t + TICK);
        while ((float)r->frame / FPS < r->t0 + t + TICK - 1e-4f) {
            fprintf(r->txt, "%s\n", line);
            r->frame++;
        }
    }
    r->t0 += seconds;
}

static void film_tick(game_t const* g, int ev, float now, void* ctx) {
    rec_t*    r = ctx;
    int const k = r->step++;
    // On the exit the game says so, and after MESSAGE_S moves on; here the
    // player stands on it, and it would say so every step.
    if (ev & PL_EV_EXIT) {
        if (r->exit_t < 0.0f)
            r->exit_t = now;
        else
            ev &= ~PL_EV_EXIT;
    }
    // Moving on, but not over GLaDOS: a chamber solved in seconds would cut
    // her line off with the next one's.
    if (r->exit_t >= 0.0f && now > r->exit_t + MESSAGE_S && (!sound_saying() || now > r->exit_t + 15.0f)) return;
    r->last = now;
    sound_events(ev);
    sound_update();
    float const film = r->t0 + now;
    sound_to(r, film);

    char const* said = NULL;
    int const   n    = sound_turret_said(&said);
    if (n != r->sub_seen) {
        r->sub_seen = n;
        r->sub      = said;
        r->sub_t    = now;
    }

    if (film + 1e-4f < (float)r->frame / FPS) return;
    static game_t shot;
    shot          = *g;
    shot.pl.yaw   = k < s_n ? s_cam[k][0] : g->pl.yaw;
    shot.pl.pitch = k < s_n ? s_cam[k][1] : g->pl.pitch;
    for (int p = 0; p < W * H; p++) s_px[p] = 0xFF000000u;
    render_set_level(&shot.lv, shot.portals);
    render_set_time(now);
    render_frame(NULL, &shot);
    char name[32];
    snprintf(name, sizeof(name), "movie_%05d", r->pic);
    save(name);
    // The HUD: how much of the story is typed (-1: gone), a turret's words,
    // and the message in the middle.
    int const len   = (int)strlen(r->story);
    int       shown = (int)(now * STORY_CPS);
    if (shown > len) shown = len;
    if (len == 0 || (now > (float)len / STORY_CPS + STORY_HOLD && !sound_saying())) shown = -1;
    char const* msg = r->exit_t >= 0.0f ? r->done : now < MESSAGE_S ? g->lv.name : "";
    fprintf(r->txt, "P\t%d\t%d\t%s\t%s\n", r->pic, shown, r->sub != NULL && now - r->sub_t < TURRET_SUB ? r->sub : "",
            msg);
    r->pic++;
    r->frame++;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: host_movie SECONDS DEMO [DEMO...]\n");
        return 1;
    }
    render_init("textures");
    char const* extra = getenv("PORTALS_CHAMBERS");
    if (extra != NULL) chamber_load_dir(extra);
    for (int a = 2; a < argc; a++)
        if (demo_find(argv[a]) < 0) {
            fprintf(stderr, "no demo %s\n", argv[a]);
            return 1;
        }
    char const* build = getenv("BUILD");
    char        path[256];
    snprintf(path, sizeof(path), "%s/movie.wav", build != NULL && build[0] ? build : "build");
    static rec_t r;
    r.wav = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/movie.txt", build != NULL && build[0] ? build : "build");
    r.txt = fopen(path, "w");
    if (r.wav == NULL || r.txt == NULL) {
        perror(path);
        return 1;
    }
    uint8_t hdr[44] = {0};
    fwrite(hdr, 1, sizeof(hdr), r.wav);  // filled in at the end

    sound_init();
    sound_set_music(true);
    sound_set_effects(true);
    sound_set_voice(true);

    float const cap = (float)atof(argv[1]);
    titles(&r, "C", OPEN_S);
    for (int a = 2; a < argc; a++) {
        int const     i = demo_find(argv[a]);
        static game_t g;
        game_load(&g, demo_chamber(i));
        static char story[sizeof(g.lv.story)];
        snprintf(story, sizeof(story), "%s", g.lv.story);
        fprintf(r.txt, "S\t%s\t%s\t%s\n", g.lv.name, g.lv.hint, story);
        static demo_state_t st;
        // Paced, unless that misses the exit: a solution timed to a moving
        // platform or a crusher keeps the script's own timing.
        float               pace = PACE_S;
        plan_t              p    = {false};
        s_n                      = 0;
        demo_run(i, cap, TICK, &st, log_tick, &p, pace);
        if (!p.exited) {
            fprintf(stderr, "%s: paced, it misses the exit; filmed at the script's own pace\n", argv[a]);
            pace = 0.0f;
            s_n  = 0;
            demo_run(i, cap, TICK, &st, log_tick, &p, pace);
        }
        plan_camera();
        r.story  = story;
        r.exit_t = -1.0f;
        r.sub    = NULL;
        r.last   = 0.0f;
        r.step   = 0;
        r.done   = a + 1 < argc ? "Chamber complete" : "All chambers complete. Cake later.";
        sound_say(story[0] ? story : NULL);
        demo_run(i, cap, TICK, &st, film_tick, &r, pace);
        r.t0 += r.last;
    }
    titles(&r, "E", CLOSE_S);

    uint32_t const data = (uint32_t)(r.frames_out * 4), rate = AUDIO_SAMPLE_RATE_HZ;
    uint32_t const h[] = {0x46464952u, 36 + data, 0x45564157u, 0x20746d66u, 16,  0x00020001u,
                          rate,        rate * 4,  0x00100004u, 0x61746164u, data};
    fseek(r.wav, 0, SEEK_SET);
    fwrite(h, sizeof(h), 1, r.wav);
    fclose(r.wav);
    fclose(r.txt);
    printf("%d frames (%d pictures), %.1f s of sound\n", r.frame, r.pic, (double)r.frames_out / AUDIO_SAMPLE_RATE_HZ);
    return 0;
}
