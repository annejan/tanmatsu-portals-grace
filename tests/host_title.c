// Host title: the badge starting up, for tools/title_film.py -- the sound
// of the splash screens (their pictures are `make splash`'s), then the
// title screen as main.c runs it, pictures and sound.
//
//   host_title ENGINE_FRAMES LOADER_FRAMES PORTALS_FRAMES TITLE_SECONDS FD
//
// The splashes, at 10 frames a second as tests/host_splash.c draws them:
// the engine's in silence (se_splash runs before on_init's sound_init),
// then Graceloader's (loadsplash.c's ticks) and the game's (splash.c's
// shots), the menu music on from the first of them, as main.c's
// music_where has it. LOADER_FRAMES and PORTALS_FRAMES are how many
// pictures `make splash` made of those two: worked out again here, they
// must agree, or the pictures are of some other build.
//
// Then main.c's to_title and its title frames, TITLE_SECONDS of them at
// 30 a second, a step of the attract mode each (attract.c steps at 30 a
// second): on_update's title path -- hud_tick_message, attract_update,
// menu_frame's menu_update, music_where -- and on_render's: the lift
// stations as play shows them, the run through attract_camera, at a
// quarter of the pixels as the badge's default setting draws it (main.c's
// half-size layer), hud_fade by attract_lit, hud_draw_title and menu_draw
// (tests/title_ui.c, on an RGB565 framebuffer laid out as the badge's).
// Each picture goes out on file descriptor FD as raw 800 x 480 RGB, and
// the sound of the whole start-up to $HOST_TITLE_OUT/title.wav, mixed as
// the badge's mixer mixes it (tests/host_movie.c's mixer).
//
//   HOST_TITLE_SEED      the attract mode's shuffle (main.c seeds it from
//                        the clock; here one is picked), default 1
//   HOST_TITLE_CONTINUE  the chamber Continue goes back to ("" none)
//   HOST_TITLE_PACKS     story packs, as on the card (Stories), default dlc
//   HOST_TITLE_KEYS      keys on the title, seconds after it comes up:
//                        "SECONDS:K,..." K u/d a cursor key, o Enter, b Esc
//   HOST_TITLE_FULL=1    the 3D at full resolution, not a quarter
//   GRACELOADER_META     as for host_splash: Graceloader's metadata.json
//
// FD -1 draws nothing: which chambers the attract mode plays, and when,
// to stderr, for picking a seed.

#define main host_shot_main
#include "host_shot.c"
#undef main

#include <unistd.h>
#include "attract.h"
#include "hud.h"
#include "lift.h"
#include "loadsplash.h"
#include "pack.h"
#include "se_audio_dsp.h"
#include "sound.h"
#include "splash.h"
#include "title_ui.h"

#define SPLASH_DT (1.0f / 10.0f)  // tests/host_splash.c's frames
#define TITLE_FPS 30

// --- The mixer, as tests/host_movie.c's ------------------------------------

#define SLOTS 8
static sfx_voice_t*    s_voices[SLOTS];
static music_source_t* s_music;
static bool            s_music_on = true, s_group_on[SE_AUDIO_SFX_GROUP_COUNT] = {true, true, true, true};
static int             s_music_vol = 100;

// The engine's, at boot: its DSP's tables (the sine wave the effects and
// the music are made of) are filled before anything plays.
esp_err_t audio_mixer_init(void) {
    return audio_dsp_init();
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

// --- The clock: the film's, the sound's and the engine's ---------------------

static FILE*   s_wav;
static long    s_frames_out;  // audio frames written
static double  s_t;           // seconds into the film
static int64_t s_now_us;

int64_t esp_timer_get_time(void) {
    return s_now_us;
}

// The next `dt` seconds of the film: its sound -- none before the mixer
// is up (`quiet`).
static void advance(double dt, bool quiet) {
    s_t      += dt;
    s_now_us  = (int64_t)(s_t * 1e6 + 0.5);
    long const want = (long)(s_t * AUDIO_SAMPLE_RATE_HZ + 0.5);
    while (s_frames_out < want) {
        int16_t buf[2 * 1024];
        size_t  n = (size_t)(want - s_frames_out);
        if (n > 1024) n = 1024;
        if (quiet)
            memset(buf, 0, sizeof(int16_t) * 2 * n);
        else
            mix(buf, n);
        fwrite(buf, sizeof(int16_t), 2 * n, s_wav);
        s_frames_out += (long)n;
    }
}

// main.c's music_where and its music_mood, where they always are here:
// the splashes and the title are the menu's -- the game's own tune, the
// desk's mood, Settings -> Menu music.
static int s_music_want = -1;

static void music_where(void) {
    int const want = title_ui_menu_music() ? 1 : 0;
    sound_music_tune(NULL);
    sound_music_mood(SOUND_MOOD_DESK);
    if (want != s_music_want) sound_set_music(want != 0);
    s_music_want = want;
}

// --- The title's picture --------------------------------------------------

static lift_sites_t  s_sites;
static lift_depart_t s_depart;
static uint32_t      s_sites_of = (uint32_t)-1;

static void title_picture(game_t* g, FILE* out) {
    // The lift stations, as on_render draws them in play: the chamber's,
    // worked out once a load (main.c's sites_now), its car at the start.
    if (g->loaded != s_sites_of) {
        lift_sites(&g->lv, &s_sites);
        s_sites_of = g->loaded;
        lift_depart_reset(&s_depart);
    }
    lift_view_t view;
    lift_view(NULL, &s_sites, &s_depart, &view);
    render_set_lift(&view);
    char const* full    = getenv("HOST_TITLE_FULL");
    bool const  quarter = title_ui_half() && !(full != NULL && full[0] == '1');
    s_quarter           = quarter ? 1 : 0;
    for (int p = 0; p < W * H; p++) s_px[p] = 0xFF000000u;
    float const yaw = g->pl.yaw, pitch = g->pl.pitch;
    attract_camera(g, &g->pl.yaw, &g->pl.pitch);
    render_frame(NULL, g);
    g->pl.yaw   = yaw;
    g->pl.pitch = pitch;
    title_ui_frame(s_px, quarter, attract_lit(), g);
    static uint8_t rgb[W * H * 3];
    for (int i = 0; i < W * H; i++) {
        rgb[3 * i]     = (uint8_t)(s_px[i] >> 16);
        rgb[3 * i + 1] = (uint8_t)(s_px[i] >> 8);
        rgb[3 * i + 2] = (uint8_t)s_px[i];
    }
    if (fwrite(rgb, 1, sizeof(rgb), out) != sizeof(rgb)) {
        perror("host_title: the pictures' pipe");
        exit(1);
    }
}

// HOST_TITLE_KEYS: when each key goes down, and which.
#define KEYS_MAX 64
static float s_key_t[KEYS_MAX];
static char  s_key[KEYS_MAX];
static int   s_keys;

static void read_keys(void) {
    char const* k = getenv("HOST_TITLE_KEYS");
    while (k != NULL && *k && s_keys < KEYS_MAX) {
        char* end;
        float const t = strtof(k, &end);
        if (end == k || *end != ':' || end[1] == '\0' || strchr("udob", end[1]) == NULL) {
            fprintf(stderr, "HOST_TITLE_KEYS: SECONDS:K, K one of u d o b, comma-separated\n");
            exit(2);
        }
        s_key_t[s_keys] = t;
        s_key[s_keys]   = end[1];
        s_keys++;
        k = end + 2;
        if (*k == ',') k++;
    }
}

int main(int argc, char** argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: host_title ENGINE_FRAMES LOADER_FRAMES PORTALS_FRAMES TITLE_SECONDS FD\n");
        return 2;
    }
    int const   engine_n = atoi(argv[1]), loader_want = atoi(argv[2]), portals_want = atoi(argv[3]);
    float const title_s  = (float)atof(argv[4]);
    int const   fd       = atoi(argv[5]);
    FILE*       out      = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (fd >= 0 && out == NULL) {
        perror("host_title: FD");
        return 1;
    }
    char const* dir = getenv("HOST_TITLE_OUT");
    char        path[512];
    snprintf(path, sizeof(path), "%s/title.wav", dir != NULL && dir[0] ? dir : "build/title");
    s_wav = fopen(path, "wb");
    if (s_wav == NULL) {
        perror(path);
        return 1;
    }
    uint8_t hdr[44] = {0};
    fwrite(hdr, 1, sizeof(hdr), s_wav);  // filled in at the end
    read_keys();

    // se_run, before on_init: the mixer (here, its DSP's tables).
    if (audio_mixer_init() != ESP_OK) {
        fprintf(stderr, "audio_dsp_init failed\n");
        return 1;
    }
    // main.c's on_init, as far as the title needs it: the textures, the
    // settings, the story packs on the card.
    render_init("textures");
    char const* cont = getenv("HOST_TITLE_CONTINUE");
    title_ui_init(cont);
    char const* packs = getenv("HOST_TITLE_PACKS");
    pack_load(packs != NULL ? packs : "dlc");

    // The engine's splash: before sound_init, in silence.
    for (int i = 0; i < engine_n; i++) advance(SPLASH_DT, true);
    sound_init();
    sound_set_effects(true);  // settings_effects() and _voice(): on, as they ship
    sound_set_voice(true);
    char const*   seed_s = getenv("HOST_TITLE_SEED");
    unsigned long seed   = seed_s != NULL && seed_s[0] ? strtoul(seed_s, NULL, 0) : 1ul;
    attract_seed((uint32_t)seed);

    // Graceloader's, read as host_splash reads it, a frame of update_frame
    // each: sound_update, then the splash, then music_where.
    loadsplash_info_t info;
    loadsplash_read("build/app.so", getenv("GRACELOADER_META"), "com.annejan.portals/app.so", &info);
    loadsplash_start(&info);
    static game_t g;
    int           loader_n = 0, portals_n = 0;
    float         clock    = 0.0f;
    for (;;) {
        sound_update();
        clock += SPLASH_DT;
        render_set_time(clock);
        bool const on = loadsplash_update(SPLASH_DT);
        if (!on) break;
        music_where();
        advance(SPLASH_DT, false);
        loader_n++;
    }
    // The game's: game_splash, in the frame Graceloader's ended.
    if (!splash_start(&g)) {
        fprintf(stderr, "splash_start: the corridor would not build\n");
        return 1;
    }
    render_set_level(&g.lv, g.portals);
    for (;;) {
        if (portals_n > 0) {
            sound_update();
            clock += SPLASH_DT;
            render_set_time(clock);
        }
        if (!splash_update(&g, SPLASH_DT)) break;
        music_where();
        advance(SPLASH_DT, false);
        portals_n++;
    }
    if (loader_n != loader_want || portals_n != portals_want) {
        fprintf(stderr,
                "host_title: Graceloader's splash is %d frames and the game's %d here, %d and %d in the pictures:"
                " `make splash` again (with the same GRACELOADER_META)\n",
                loader_n, portals_n, loader_want, portals_want);
        return 1;
    }

    // update_frame, the splash over: to the title (main.c's to_title).
    render_set_portal_depth(title_ui_portal_depth());
    sound_hush();
    hud_quiet();
    title_ui_open();
    attract_begin(&g);
    render_set_level(&g.lv, g.portals);
    int runs = attract_runs();
    fprintf(stderr, "%6.2f s  %s\n", s_t, g.lv.name);
    if (out != NULL) title_picture(&g, out);  // that frame's on_render
    advance(1.0 / TITLE_FPS, false);

    float const dt   = 1.0f / TITLE_FPS;
    int const   n    = (int)(title_s * TITLE_FPS + 0.5f);
    double const t0  = s_t;
    for (int k = 1; k < n; k++) {
        float const into = (float)(s_t - t0);
        // on_input: the keys come in before the frame, as on the badge.
        for (int i = 0; i < s_keys; i++)
            if (s_key_t[i] <= into && s_key_t[i] > into - dt) {
                title_ui_key(s_key[i]);
                fprintf(stderr, "key %c at %.2f s\n", s_key[i], s_t);
            }
        // update_frame on the title.
        sound_update();
        clock = fmodf(clock + dt, 3600.0f);
        render_set_time(clock);
        hud_tick_message(dt);
        if (attract_update(&g, dt)) render_set_level(&g.lv, g.portals);
        title_ui_update();
        music_where();
        if (attract_runs() != runs) {
            runs = attract_runs();
            fprintf(stderr, "%6.2f s  %s\n", s_t, g.lv.name);
        }
        if (out != NULL) title_picture(&g, out);
        advance(dt, false);
    }
    if (out != NULL) fclose(out);

    uint32_t const data = (uint32_t)(s_frames_out * 4), rate = AUDIO_SAMPLE_RATE_HZ;
    uint32_t const h[]  = {0x46464952u, 36 + data, 0x45564157u, 0x20746d66u, 16,  0x00020001u,
                           rate,        rate * 4,  0x00100004u, 0x61746164u, data};
    fseek(s_wav, 0, SEEK_SET);
    fwrite(h, sizeof(h), 1, s_wav);
    fclose(s_wav);
    fprintf(stderr, "splashes %d + %d + %d frames, title %d frames; %.1f s of sound; %d runs, %d to the exit\n",
            engine_n, loader_n, portals_n, n, (double)s_frames_out / AUDIO_SAMPLE_RATE_HZ, attract_runs(),
            attract_exits());
    return 0;
}
