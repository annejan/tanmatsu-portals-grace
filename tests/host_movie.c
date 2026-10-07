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

#include "cine.h"
#include "sound.h"

#define FPS        10
#define TICK       s_tick  // a step: 1/50 s, or HOST_MOVIE_TICK -- a TAS's, the badge's 0.1
#define STORY_CPS  30.0f   // as hud.c types the story line ...
#define STORY_HOLD 5.0f    // ... and holds it
#define TURRET_SUB 2.5f    // ... and a turret's words
#define MESSAGE_S  2.5f    // ... and "Chamber complete": then the next chamber
#define TAS_HOLD   1.2f    // a TAS cuts that short: the timer has stopped
#define OPEN_S     4.0f    // the opening and closing titles
#define CLOSE_S    4.5f
#define MAX_TICKS  (200 * 50)

static float s_tick = 1.0f / 50.0f;

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
// Worked out ahead, from a first run of each (main/cine.h).

#define PACE_S 0.8f  // the player stands this long before each act, looking

static cine_view_t s_log[MAX_TICKS];
static float       s_cam[MAX_TICKS][2];
static int         s_n;

typedef struct {
    bool exited;
} plan_t;

static void log_tick(game_t const* g, int ev, float now, void* ctx) {
    (void)now;
    if (ev & PL_EV_EXIT) ((plan_t*)ctx)->exited = true;
    if (s_n >= MAX_TICKS) return;
    s_log[s_n++] =
        (cine_view_t){g->pl.yaw, g->pl.pitch, g->pl.vel.x, g->pl.vel.y, g->pl.vel.z, g->pl.on_ground, g->held >= 0, ev};
}

static void plan_camera(void) {
    static float want[MAX_TICKS][2];
    static bool  aimed[MAX_TICKS];
    cine_plan(s_log, s_n, TICK, s_cam, want, aimed);
}

// --- The recording ------------------------------------------------------------
//
// $BUILD/movie.txt has a line per frame of film -- "C" the opening titles,
// "P\tN\ttyped\tturret\tmessage\ttime\texit" the picture movie_N with the
// HUD on it, `time` into the run (the exit reached at `exit`, or -1),
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
    bool        tas;     // HOST_MOVIE_TAS: a tool-assisted run, filmed as it goes
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
    // her line off with the next one's. A TAS moves on, as the game does.
    if (r->exit_t >= 0.0f && now > r->exit_t + (r->tas ? TAS_HOLD : MESSAGE_S) &&
        (r->tas || !sound_saying() || now > r->exit_t + 15.0f))
        return;
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
    fprintf(r->txt, "P\t%d\t%d\t%s\t%s\t%.2f\t%.2f\n", r->pic, shown,
            r->sub != NULL && now - r->sub_t < TURRET_SUB ? r->sub : "", msg, (double)now, (double)r->exit_t);
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

    float const cap  = (float)atof(argv[1]);
    char const* tick = getenv("HOST_MOVIE_TICK");
    if (tick != NULL && atof(tick) > 0.0) s_tick = (float)atof(tick);
    char const* tas = getenv("HOST_MOVIE_TAS");
    r.tas           = tas != NULL && tas[0] == '1';
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
        float               pace = r.tas ? 0.0f : PACE_S;
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
