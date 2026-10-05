// Host movie: a demo's run, as frames and sound, for tools/make_movie.py.
//
//   host_movie DEMO SECONDS
//
// writes $BUILD/shots/movie_NNNNN.ppm at 10 frames a second, $BUILD/movie.wav
// and $BUILD/movie.txt (per frame: what the HUD shows). The frames come
// from host_shot's rasterizer, included below, with the camera eased
// between the script's sudden turns; the sound is the game's own --
// sound.c, speech.c with SAM, the engine's music -- mixed here offline as
// the badge's mixer mixes it.

#define main host_shot_main
#include "host_shot.c"
#undef main

#include "sound.h"

#define FPS        10
#define CAM_EASE_S 0.18f  // the camera follows the view this quickly
#define STORY_CPS  30.0f  // as main.c types it ...
#define STORY_HOLD 5.0f   // ... and holds it
#define TURRET_SUB 2.5f
#define END_S      3.0f  // the film goes on this long past the exit

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

// --- The recording ------------------------------------------------------------

typedef struct {
    FILE*       wav;
    FILE*       txt;
    long        frames_out;  // audio frames written
    int         frame;       // pictures written
    float       yaw, pitch;  // the eased camera
    bool        have_cam, cut;
    char const* story;
    int         sub_seen;
    char const* sub;
    float       sub_t;
    float       exit_t;  // when the player reached the exit, or -1
} rec_t;

static float wrap(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

static void tick(game_t const* g, int ev, float now, void* ctx) {
    rec_t* r = ctx;
    // On the exit, the game moves on to the next chamber; here the player
    // stands on it, and it would say so every step.
    if (ev & PL_EV_EXIT) {
        if (r->exit_t < 0.0f)
            r->exit_t = now;
        else
            ev &= ~PL_EV_EXIT;
    }
    if (r->exit_t >= 0.0f && now > r->exit_t + END_S) return;
    sound_events(ev);
    sound_update();
    if (ev & PL_EV_TELEPORT) r->cut = true;

    // The sound up to now.
    long const want = (long)(now * (float)AUDIO_SAMPLE_RATE_HZ + 0.5f);
    while (r->frames_out < want) {
        int16_t buf[2 * 1024];
        size_t  n = (size_t)(want - r->frames_out);
        if (n > 1024) n = 1024;
        mix(buf, n);
        fwrite(buf, sizeof(int16_t), 2 * n, r->wav);
        r->frames_out += (long)n;
    }

    char const* said = NULL;
    int const   n    = sound_turret_said(&said);
    if (n != r->sub_seen) {
        r->sub_seen = n;
        r->sub      = said;
        r->sub_t    = now;
    }

    // A picture every 1/FPS s, through the eased camera.
    if (now + 1e-4f < (float)r->frame / FPS) return;
    static game_t shot;
    shot = *g;
    if (!r->have_cam || r->cut) {
        r->yaw   = g->pl.yaw;
        r->pitch = g->pl.pitch;
    } else {
        float const k  = 1.0f - expf(-1.0f / FPS / CAM_EASE_S);
        r->yaw        += wrap(g->pl.yaw - r->yaw) * k;
        r->pitch      += (g->pl.pitch - r->pitch) * k;
    }
    r->have_cam   = true;
    r->cut        = false;
    shot.pl.yaw   = r->yaw;
    shot.pl.pitch = r->pitch;
    for (int p = 0; p < W * H; p++) s_px[p] = 0xFF000000u;
    render_set_level(&shot.lv, shot.portals);
    render_set_time(now);
    render_frame(NULL, &shot);
    char name[32];
    snprintf(name, sizeof(name), "movie_%05d", r->frame);
    save(name);
    // The HUD: how much of the story is typed (-1: gone), and a turret's words.
    int const len   = (int)strlen(r->story);
    int       shown = (int)(now * STORY_CPS);
    if (shown > len) shown = len;
    if (len == 0 || (now > (float)len / STORY_CPS + STORY_HOLD && !sound_saying())) shown = -1;
    fprintf(r->txt, "%d\t%s\n", shown, r->sub != NULL && now - r->sub_t < TURRET_SUB ? r->sub : "");
    r->frame++;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: host_movie DEMO SECONDS\n");
        return 1;
    }
    render_init("textures");
    char const* extra = getenv("PORTALS_CHAMBERS");
    if (extra != NULL) chamber_load_dir(extra);
    int const i = demo_find(argv[1]);
    if (i < 0) {
        fprintf(stderr, "no demo %s\n", argv[1]);
        return 1;
    }
    char const* build = getenv("BUILD");
    char        path[256];
    snprintf(path, sizeof(path), "%s/movie.wav", build != NULL && build[0] ? build : "build");
    rec_t r = {.wav = fopen(path, "wb"), .exit_t = -1.0f};
    snprintf(path, sizeof(path), "%s/movie.txt", build != NULL && build[0] ? build : "build");
    r.txt = fopen(path, "w");
    if (r.wav == NULL || r.txt == NULL) {
        perror(path);
        return 1;
    }
    static game_t g;
    game_load(&g, demo_chamber(i));
    r.story = g.lv.story;
    fprintf(r.txt, "%s\n", r.story);
    uint8_t hdr[44] = {0};
    fwrite(hdr, 1, sizeof(hdr), r.wav);  // filled in at the end

    sound_init();
    sound_set_music(true);
    sound_set_effects(true);
    sound_set_voice(true);
    sound_say(r.story[0] ? r.story : NULL);

    static demo_state_t st;
    demo_run(i, (float)atof(argv[2]), 1.0f / 50.0f, &st, tick, &r);

    uint32_t const data = (uint32_t)(r.frames_out * 4), rate = AUDIO_SAMPLE_RATE_HZ;
    uint32_t const h[] = {0x46464952u, 36 + data, 0x45564157u, 0x20746d66u, 16,  0x00020001u,
                          rate,        rate * 4,  0x00100004u, 0x61746164u, data};
    fseek(r.wav, 0, SEEK_SET);
    fwrite(h, sizeof(h), 1, r.wav);
    fclose(r.wav);
    fclose(r.txt);
    printf("%d frames, %.1f s of sound\n", r.frame, (double)r.frames_out / AUDIO_SAMPLE_RATE_HZ);
    return 0;
}
