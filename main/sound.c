#include "sound.h"
#include <math.h>
#include <string.h>
#include "game.h"
#include "synthengine3d.h"

#define SR       ((float)AUDIO_SAMPLE_RATE_HZ)
#define POOL     6  // of the mixer's 8 voice slots; the rest stay free
#define GROUP_FX 0

// --- One effect -----------------------------------------------------------
//
// A tone or a noise, swept in pitch from f0 to f1 over its length, shaped
// by a quick attack and a falling tail, through an optional low-pass. Up
// to four notes in a row make a jingle.

typedef enum {
    W_SINE,
    W_SQUARE,
    W_SAW,
    W_TRI,
    W_NOISE
} wave_t;

typedef struct {
    wave_t wave;
    float  f0, f1;    // Hz, swept exponentially over the note
    float  dur;       // seconds, per note
    float  attack;    // seconds
    float  gain;      // 0..1
    float  lowpass;   // Hz, 0 for none
    float  notes[4];  // a jingle: each note's f0 (f1 = f0); 0 ends it
} recipe_t;

static recipe_t const s_recipe[SND_COUNT] = {
    [SND_SHOT_BLUE]   = {W_SAW, 380, 1250, 0.16f, 0.005f, 0.30f, 3500, {0}},
    [SND_SHOT_ORANGE] = {W_SAW, 260, 820, 0.18f, 0.005f, 0.30f, 2800, {0}},
    [SND_SHOT_FAIL]   = {W_SQUARE, 110, 90, 0.14f, 0.003f, 0.22f, 900, {0}},
    [SND_TELEPORT]    = {W_NOISE, 2400, 500, 0.28f, 0.02f, 0.35f, 2400, {0}},
    [SND_LAND]        = {W_SINE, 95, 40, 0.16f, 0.002f, 0.55f, 0, {0}},
    [SND_PICKUP]      = {W_TRI, 520, 880, 0.09f, 0.003f, 0.30f, 0, {0}},
    [SND_DROP]        = {W_TRI, 700, 380, 0.09f, 0.003f, 0.30f, 0, {0}},
    [SND_BUTTON_DOWN] = {W_SQUARE, 330, 220, 0.12f, 0.002f, 0.22f, 1500, {0}},
    [SND_BUTTON_UP]   = {W_SQUARE, 220, 300, 0.10f, 0.002f, 0.18f, 1500, {0}},
    [SND_DOOR]        = {W_SAW, 70, 95, 0.45f, 0.05f, 0.30f, 600, {0}},
    [SND_DEATH]       = {W_SAW, 420, 70, 0.90f, 0.01f, 0.30f, 1800, {0}},
    [SND_COMPLETE]    = {W_TRI, 0, 0, 0.14f, 0.005f, 0.35f, 0, {523.25f, 659.25f, 783.99f, 1046.5f}},
    [SND_MENU]        = {W_SINE, 900, 900, 0.04f, 0.002f, 0.18f, 0, {0}},
    [SND_FIZZLE]      = {W_NOISE, 6000, 2000, 0.35f, 0.005f, 0.30f, 5000, {0}},
    [SND_LAUNCH]      = {W_SINE, 120, 520, 0.35f, 0.005f, 0.50f, 0, {0}},
    [SND_TICK]        = {W_SQUARE, 1800, 1800, 0.03f, 0.001f, 0.20f, 4000, {0}},
    [SND_DROPPER]     = {W_NOISE, 900, 200, 0.30f, 0.01f, 0.35f, 1200, {0}},
    [SND_BURN]        = {W_NOISE, 4000, 3000, 0.45f, 0.005f, 0.30f, 6000, {0}},
    [SND_BOUNCE]      = {W_SINE, 180, 640, 0.22f, 0.003f, 0.45f, 0, {0}},
};

typedef struct {
    sfx_voice_t    base;  // the mixer's contract; first
    recipe_t       r;
    bool           used;
    volatile bool  reaped;  // the mixer has dropped it from its table (fx_reaped)
    uint32_t       t, n;    // samples played of this note, and its length
    int            note;    // jingle: which note
    float          inc, k;  // phase increment, and its factor per sample (the sweep)
    uint32_t       phase, noise;
    audio_biquad_t lp;
} fx_t;

static fx_t s_pool[POOL];
static bool s_fx_on = true;

static void start_note(fx_t* v) {
    float const f0 = v->r.notes[0] > 0 ? v->r.notes[v->note] : v->r.f0;
    float const f1 = v->r.notes[0] > 0 ? f0 : v->r.f1;
    v->t           = 0;
    v->n           = (uint32_t)(v->r.dur * SR);
    v->inc         = (float)audio_dsp_phase_inc(f0);
    v->k           = v->n > 0 ? powf(f1 / f0, 1.0f / (float)v->n) : 1.0f;
}

// On the mixer's task: no waiting, no logging.
static void fx_render(sfx_voice_t* self, int16_t* out, size_t frames) {
    fx_t* v = (fx_t*)self;
    for (size_t i = 0; i < frames; i++) {
        if (v->t >= v->n) {
            if (v->r.notes[0] > 0 && v->note < 3 && v->r.notes[v->note + 1] > 0) {
                v->note++;
                start_note(v);
            } else {
                self->finished = true;
                return;
            }
        }
        float s;
        switch (v->r.wave) {
            case W_SQUARE:
                s = audio_dsp_square(v->phase);
                break;
            case W_SAW:
                s = audio_dsp_saw(v->phase);
                break;
            case W_TRI:
                s = audio_dsp_triangle(v->phase);
                break;
            case W_NOISE:
                s = audio_dsp_noise(&v->noise);
                break;
            default:
                s = audio_dsp_sin(v->phase);
                break;
        }
        if (v->r.lowpass > 0) s = audio_biquad_tick(&v->lp, s);
        float const   x    = (float)v->t / (float)v->n;
        float const   att  = v->r.attack > 0 ? fminf(1.0f, (float)v->t / (v->r.attack * SR)) : 1.0f;
        float const   env  = att * (1.0f - x) * (1.0f - x);
        int16_t const o    = audio_dsp_to_s16(s * env * v->r.gain);
        out[2 * i]         = o;
        out[2 * i + 1]     = o;
        v->phase          += (uint32_t)v->inc;
        v->inc            *= v->k;
        v->t++;
    }
}

// Called by the mixer, under its lock, as it drops a finished voice from
// its table: only then may the voice be registered again.
static void fx_reaped(sfx_voice_t* self) {
    ((fx_t*)self)->reaped = true;
}

void sound_play(sound_t s) {
    if (!s_fx_on || s < 0 || s >= SND_COUNT) return;
    // A voice the mixer still holds must not be registered again: it
    // would sit in two slots and play twice as fast. A voice is free once
    // the mixer has said it dropped it -- not when it looks finished,
    // which a voice muted by the effects switch never is.
    fx_t* v = NULL;
    for (int i = 0; i < POOL && v == NULL; i++)
        if (!s_pool[i].used || s_pool[i].reaped) v = &s_pool[i];
    if (v == NULL) return;  // all busy: this one goes unheard
    memset(v, 0, sizeof(*v));
    v->base.render   = fx_render;
    v->base.shutdown = fx_reaped;
    v->base.group    = GROUP_FX;
    v->r             = s_recipe[s];
    v->noise         = 0x9E3779B9u ^ (uint32_t)s;
    if (v->r.lowpass > 0) audio_biquad_lpf(&v->lp, v->r.lowpass, 0.707f);
    start_note(v);
    v->used = audio_mixer_register_voice(&v->base);
}

void sound_events(int ev) {
    if (ev & GAME_EV_SHOT_BLUE) sound_play(SND_SHOT_BLUE);
    if (ev & GAME_EV_SHOT_ORANGE) sound_play(SND_SHOT_ORANGE);
    if (ev & GAME_EV_SHOT_FAIL) sound_play(SND_SHOT_FAIL);
    if (ev & PL_EV_TELEPORT) sound_play(SND_TELEPORT);
    if (ev & PL_EV_LANDED) sound_play(SND_LAND);
    if (ev & GAME_EV_PICKUP) sound_play(SND_PICKUP);
    if (ev & GAME_EV_DROP) sound_play(SND_DROP);
    if (ev & (GAME_EV_BUTTON_DOWN | GAME_EV_PRESS)) sound_play(SND_BUTTON_DOWN);
    if (ev & GAME_EV_BUTTON_UP) sound_play(SND_BUTTON_UP);
    if (ev & GAME_EV_DOOR) sound_play(SND_DOOR);
    if (ev & PL_EV_DIED) sound_play(SND_DEATH);
    if (ev & PL_EV_EXIT) sound_play(SND_COMPLETE);
    if (ev & GAME_EV_FIZZLE) sound_play(SND_FIZZLE);
    if (ev & GAME_EV_LAUNCH) sound_play(SND_LAUNCH);
    if (ev & GAME_EV_TICK) sound_play(SND_TICK);
    if (ev & GAME_EV_DROPPER) sound_play(SND_DROPPER);
    if (ev & GAME_EV_BURN) sound_play(SND_BURN);
    if (ev & PL_EV_BOUNCE) sound_play(SND_BOUNCE);
}

// --- Music ----------------------------------------------------------------
//
// The engine's procedural generator, slowed and softened: the synthwave
// preset's progressions and arps, but at 76-88 BPM, a soft sine arp, a
// warm pad in front, a kick on the one and nothing else for drums.

static se_music_config_t             s_music;
static se_music_drum_pattern_t const s_drums[] = {
    {.kick = 0x0001, .snare = 0x0000, .hat = 0x0000},
    {.kick = 0x0101, .snare = 0x0000, .hat = 0x0000},
};
static uint16_t const s_bass[] = {0x0101, 0x0001};

void sound_init(void) {
    s_music                     = *se_music_synthwave_preset();
    s_music.bpm_min             = 76;
    s_music.bpm_span            = 12;
    s_music.drum_patterns       = s_drums;
    s_music.drum_pattern_count  = (int)(sizeof(s_drums) / sizeof(s_drums[0]));
    s_music.bass_patterns       = s_bass;
    s_music.bass_pattern_count  = (int)(sizeof(s_bass) / sizeof(s_bass[0]));
    s_music.arp.osc             = SE_OSC_SINE;
    s_music.arp.osc_count       = 1;
    s_music.arp.gain           *= 0.5f;
    s_music.arp.env.release     = 0.6f;
    s_music.bass.filter         = SE_FILTER_LPF;
    s_music.bass.cutoff_hz      = 300.0f;
    s_music.pad.gain           *= 1.3f;
    s_music.pad.amp_lfo_hz      = 0.15f;
    s_music.pad.amp_lfo_depth   = 0.3f;
    s_music.kick.gain          *= 0.6f;
    s_music.snare_voice         = NULL;
    s_music.hat_voice           = NULL;
    audio_mixer_set_music(music_procedural_create(&s_music, 0x9047A1u));
    audio_mixer_set_music_volume(60);
}

void sound_set_music(bool on) {
    audio_mixer_set_music_enabled(on);
}

void sound_set_effects(bool on) {
    s_fx_on = on;
    audio_mixer_set_group_enabled(GROUP_FX, on);
}
