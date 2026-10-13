#include "sound.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "speech.h"
#include "synthengine3d.h"

#define SR       ((float)AUDIO_SAMPLE_RATE_HZ)
#define POOL     6  // of the mixer's 8 voice slots; the other two speak (below)
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
    [SND_PELLET]      = {W_TRI, 900, 300, 0.10f, 0.002f, 0.25f, 0, {0}},
    [SND_CAUGHT]      = {W_TRI, 0, 0, 0.12f, 0.005f, 0.35f, 0, {392.0f, 523.25f, 783.99f, 0}},
    [SND_CRUSH]       = {W_NOISE, 400, 60, 0.35f, 0.002f, 0.60f, 700, {0}},
    [SND_TURRET_SPOT] = {W_SINE, 1600, 1600, 0.15f, 0.002f, 0.30f, 0, {0}},
    [SND_TURRET_SHOT] = {W_NOISE, 3000, 900, 0.05f, 0.001f, 0.35f, 4000, {0}},
    [SND_TOPPLE]      = {W_SAW, 520, 110, 0.35f, 0.005f, 0.30f, 1500, {0}},
    [SND_DING]        = {W_SINE, 0, 0, 0.32f, 0.003f, 0.20f, 0, {1318.5f, 1046.5f, 0, 0}},
    [SND_HUM]         = {W_SAW, 52, 74, 1.80f, 0.25f, 0.22f, 380, {0}},
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

static void turret_says(char const* const lines[]);
static void music_events(int ev);
static void music_frame(void);

void sound_events(int ev) {
    music_events(ev);
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
    if (ev & GAME_EV_PELLET) sound_play(SND_PELLET);
    if (ev & GAME_EV_CAUGHT) sound_play(SND_CAUGHT);
    if (ev & GAME_EV_CRUSH) sound_play(SND_CRUSH);
    if (ev & GAME_EV_SHOOT) sound_play(SND_TURRET_SHOT);
    if (ev & GAME_EV_SPOTTED) {
        sound_play(SND_TURRET_SPOT);
        turret_says(speech_turret_spot);
    }
    if (ev & GAME_EV_TOPPLE) {
        sound_play(SND_TOPPLE);
        turret_says(speech_turret_down);
    }
}

// --- GLaDOS, and the turrets ----------------------------------------------
//
// Two voices, each playing a piece SAM rendered (speech.c) at 22050 Hz,
// stepped to the mixer's rate: GLaDOS, and a turret, which talks over her
// as turrets do. SAM reuses its one buffer, so each voice plays its own
// copy; a voice's copy is only written once the mixer has dropped it.

#define SAY_PIECES 8
#define SAY_GAIN   180  // 8-bit to 16-bit, with room for the effects
#define GLADOS     0
#define TURRET     1

typedef struct {
    sfx_voice_t   base;  // first
    uint8_t*      pcm;   // its own copy of SAM's sound ...
    size_t        cap;   // ... in a buffer this big
    uint32_t      n;     // samples
    uint64_t      pos;   // 16.16 fixed point, in SAM's samples: 64-bit, as 32 bits
    uint32_t      step;  // overflow after 65536 samples -- 3 s -- and the piece starts over
    bool          used;
    volatile bool reaped;
} say_t;

static say_t s_say[2];
static bool  s_voice_on = true;
static char  s_pieces[SAY_PIECES][SPEECH_MAX];
static int   s_n_pieces, s_next;

static void say_render(sfx_voice_t* self, int16_t* out, size_t frames) {
    say_t* v = (say_t*)self;
    for (size_t i = 0; i < frames; i++) {
        uint64_t const k = v->pos >> 16;
        if (k >= v->n) {
            self->finished = true;
            return;
        }
        int16_t const o  = (int16_t)(((int)v->pcm[k] - 128) * SAY_GAIN);
        out[2 * i]       = o;
        out[2 * i + 1]   = o;
        v->pos          += v->step;
    }
}

static void say_reaped(sfx_voice_t* self) {
    ((say_t*)self)->reaped = true;
}

static bool say_busy(int who) {
    return s_say[who].used && !s_say[who].reaped;
}

// Say `piece` in `who`'s voice, which is not busy.
static void speak(int who, char const* piece) {
    int                  n   = 0;
    uint8_t const* const pcm = speech_render(piece, who == GLADOS ? SPEECH_GLADOS : SPEECH_TURRET, &n);
    if (pcm == NULL || n <= 0) return;
    say_t* const v = &s_say[who];
    if ((size_t)n > v->cap) {
        uint8_t* const grown = realloc(v->pcm, (size_t)n);
        if (grown == NULL) return;
        v->pcm = grown;
        v->cap = (size_t)n;
    }
    memcpy(v->pcm, pcm, (size_t)n);
    speech_free();
    uint8_t* const buf = v->pcm;
    size_t const   cap = v->cap;
    memset(v, 0, sizeof(*v));
    v->pcm           = buf;
    v->cap           = cap;
    v->base.render   = say_render;
    v->base.shutdown = say_reaped;
    v->base.group    = GROUP_FX;
    v->n             = (uint32_t)n;
    v->step          = (uint32_t)((float)SPEECH_RATE / SR * 65536.0f);
    v->used          = audio_mixer_register_voice(&v->base);
}

void sound_say(char const* line) {
    if (say_busy(GLADOS)) audio_mixer_stop_voice(&s_say[GLADOS].base);
    s_n_pieces = line != NULL && s_voice_on ? speech_split(line, s_pieces, SAY_PIECES) : 0;
    s_next     = 0;
}

// A turret's line, unless a turret is talking already. GLaDOS may be: it
// talks over her.
static char const* s_turret_line;
static int         s_turret_lines;

static void turret_says(char const* const lines[]) {
    static int k;
    if (say_busy(TURRET)) return;
    s_turret_line = lines[k++ % SPEECH_TURRET_LINES];
    s_turret_lines++;
    if (s_voice_on) speak(TURRET, s_turret_line);
}

int sound_turret_said(char const** line) {
    *line = s_turret_line;
    return s_turret_lines;
}

bool sound_saying(void) {
    return say_busy(GLADOS) || s_next < s_n_pieces;
}

void sound_update(void) {
    if (!say_busy(GLADOS) && s_next < s_n_pieces) speak(GLADOS, s_pieces[s_next++]);
    music_frame();
}

void sound_hush(void) {
    sound_say(NULL);
    if (say_busy(TURRET)) audio_mixer_stop_voice(&s_say[TURRET].base);
}

void sound_set_voice(bool on) {
    s_voice_on = on;
    if (!on) sound_hush();
}

// --- Music ----------------------------------------------------------------
//
// The engine's procedural generator, slowed and softened: the synthwave
// preset's progressions and arps, but at 76-88 BPM, a sine arp, a
// warm pad in front, a kick on the one and nothing else for drums. That is
// the game's own tune, "calm" -- the title, the built-in chambers, the
// editor, a story pack that names no tune of its own -- and it plays
// sample for sample as it always has: no mood touches it (below).

#define MUSIC_SEED 0x9047A1u

static se_music_config_t             s_music;
static se_music_drum_pattern_t const s_drums[] = {
    {.kick = 0x0001, .snare = 0x0000, .hat = 0x0000},
    {.kick = 0x0101, .snare = 0x0000, .hat = 0x0000},
};
static uint16_t const s_bass[] = {0x0101, 0x0001};

// --- The story packs' tunes -------------------------------------------------
//
// The same generator, other banks and voices (se_music_procedural.h): a
// pack names one in pack.txt, `music: office` (pack.h). Each is calm still,
// as the game's own is, but its own: chords, tempo, timbre. The chords: a
// triad `root` semitones over the tonic, minor (MI) or major (MA).

#define MI(root) {root, 0}
#define MA(root) {root, 1}
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// "office": Human in the loop. An Aperture terminal, late; the wall
// clock ticks at 60 BPM -- tick on one and three, tock on two and four --
// over a dark, slow pad in minor with a Phrygian half step (bII) in it,
// and a muted square blipping now and then, as a terminal does. In a
// test chamber a heartbeat comes in under it and the clock goes.
static se_music_progression_t const s_office_prog[] = {
    {{MI(0), MI(0), MA(8), MA(8), MI(5), MI(5), MI(7), MI(7)}},    // i i VI VI iv iv v v
    {{MI(0), MA(1), MI(0), MA(1), MA(8), MI(7), MI(0), MI(0)}},    // i bII i bII VI v i i
    {{MI(0), MA(8), MA(3), MI(7), MI(0), MA(8), MI(5), MA(7)}},    // i VI III v i VI iv V
    {{MI(0), MI(5), MA(8), MA(1), MI(0), MI(5), MI(7), MI(0)}},    // i iv VI bII i iv v i
    {{MI(0), MA(10), MA(8), MI(7), MI(0), MA(10), MA(8), MA(7)}},  // i VII VI v ... V
};
static se_music_arp_pattern_t const s_office_arp[] = {
    {{0, -1, -1, -1, 2, -1, -1, -1, 1, -1, -1, -1, 2, -1, -1, -1}},   // on the beat
    {{3, -1, -1, 2, -1, -1, 1, -1, -1, -1, -1, -1, 0, -1, -1, -1}},   // falling, then a gap
    {{0, -1, 2, -1, 3, -1, -1, -1, 4, -1, 3, -1, -1, -1, -1, -1}},    // a question
    {{5, -1, -1, -1, -1, -1, -1, -1, 4, -1, -1, -1, -1, -1, 3, -1}},  // a cursor, blinking
    {{0, -1, -1, -1, -1, -1, -1, -1, 0, -1, -1, -1, 1, -1, 2, -1}},   // a prompt, waiting
};
static se_music_drum_pattern_t const s_office_drums[] = {
    {.kick = 0x0303, .snare = 0x1010, .hat = 0x0101},  // lub-dub on one and three
    {.kick = 0x0003, .snare = 0x1010, .hat = 0x0101},  // ... on one
};
// clang-format off
static uint16_t const    s_office_bass[]  = {0x0001, 0x0101, 0x0001};  // a held note a bar, or two
static int8_t const      s_office_keys[]  = {45, 47, 50};             // A2, B2, D3
static se_music_config_t const s_office = {
    .bpm_min = 60, .bpm_span = 1,  // the clock: a click a second
    .tonics = s_office_keys, .tonic_count = COUNT(s_office_keys),
    .progressions = s_office_prog, .progression_count = COUNT(s_office_prog),
    .arp_patterns = s_office_arp, .arp_pattern_count = COUNT(s_office_arp),
    .drum_patterns = s_office_drums, .drum_pattern_count = COUNT(s_office_drums),
    .bass_patterns = s_office_bass, .bass_pattern_count = COUNT(s_office_bass),
    .bass  = {.osc = SE_OSC_TRIANGLE, .osc_count = 1, .env = {0.02f, 0.8f, 0.5f, 0.3f}, .gain = 0.22f},
    .arp   = {.osc = SE_OSC_SQUARE, .osc_count = 1, .env = {0.002f, 0.18f, 0.0f, 0.3f},
              .filter = SE_FILTER_LPF, .cutoff_hz = 1400.0f, .q = 0.7f, .gain = 0.22f},
    .pad   = {.osc = SE_OSC_SAW, .osc_count = 1, .env = {1.5f, 0.8f, 0.7f, 2.5f},
              .filter = SE_FILTER_LPF, .cutoff_hz = 1200.0f, .q = 0.9f, .gain = 0.34f / 3.0f,
              .amp_lfo_hz = 0.06f, .amp_lfo_depth = 0.35f},
    .kick  = {.osc = SE_OSC_SINE, .osc_count = 1, .env = {0.002f, 0.14f, 0.0f, 0.01f}, .gain = 0.26f,
              .pitch_env_amt = 60.0f},
    .snare = {.osc = SE_OSC_NOISE, .osc_count = 1, .env = {0.001f, 0.02f, 0.0f, 0.005f},
              .filter = SE_FILTER_BPF, .cutoff_hz = 1700.0f, .q = 3.0f, .gain = 0.16f},  // tock
    .hat   = {.osc = SE_OSC_NOISE, .osc_count = 1, .env = {0.001f, 0.015f, 0.0f, 0.005f},
              .filter = SE_FILTER_BPF, .cutoff_hz = 3600.0f, .q = 3.0f, .gain = 0.14f},  // tick
    .pad_detune = 0.008f,  // a little sour
};
// clang-format on

// "hackfest": Off to Hackfest. Chores, then the road: major key, 90-97
// BPM, a square arp bouncing over the octave as a chiptune does, a
// triangle bass in eighths, a triangle-organ pad; offbeat hats at the
// desk, a kick and a soft snare once a chore is under way.
static se_music_progression_t const s_hackfest_prog[] = {
    {{MA(0), MA(7), MI(9), MA(5), MA(0), MA(7), MI(9), MA(5)}},    // I V vi IV
    {{MA(0), MA(5), MI(9), MA(7), MA(0), MA(5), MA(7), MA(7)}},    // I IV vi V
    {{MI(9), MA(5), MA(0), MA(7), MI(9), MA(5), MA(0), MA(7)}},    // vi IV I V
    {{MA(0), MA(10), MA(5), MA(0), MA(0), MA(10), MA(5), MA(0)}},  // I bVII IV I
    {{MA(0), MI(4), MA(5), MA(7), MA(0), MI(4), MA(5), MA(7)}},    // I iii IV V
    {{MA(5), MA(7), MI(4), MI(9), MI(2), MA(7), MA(0), MA(0)}},    // IV V iii vi ii V I
};
static se_music_arp_pattern_t const s_hackfest_arp[] = {
    {{0, 3, 1, 3, 2, 3, 1, 3, 0, 3, 1, 3, 2, 3, 1, 3}},          // over the octave
    {{0, -1, 3, -1, 1, -1, 4, -1, 2, -1, 5, -1, 4, -1, 3, -1}},  // octave hops
    {{0, -1, 0, 2, -1, 2, 1, -1, 0, -1, 0, 2, -1, 2, 4, -1}},    // syncopated
    {{3, -1, 2, -1, 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 4, -1}},  // down and up
    {{0, 1, 2, 3, 2, 1, 0, -1, 2, 3, 4, 5, 4, 3, 2, -1}},        // scales
};
static se_music_drum_pattern_t const s_hackfest_drums[] = {
    {.kick = 0x0101, .snare = 0x1010, .hat = 0x4444},  // kick, snare, hats on the offbeat
    {.kick = 0x0901, .snare = 0x1010, .hat = 0x4444},  // ... a skip in the kick
    {.kick = 0x0101, .snare = 0x1010, .hat = 0x5555},  // ... hats on the eighths
};
// clang-format off
static uint16_t const    s_hackfest_bass[] = {0x5555, 0x1515, 0x5555, 0x1111};
static int8_t const      s_hackfest_keys[] = {48, 50, 53, 55};  // C3, D3, F3, G3
static se_music_config_t const s_hackfest = {
    .bpm_min = 90, .bpm_span = 8,
    .tonics = s_hackfest_keys, .tonic_count = COUNT(s_hackfest_keys),
    .progressions = s_hackfest_prog, .progression_count = COUNT(s_hackfest_prog),
    .arp_patterns = s_hackfest_arp, .arp_pattern_count = COUNT(s_hackfest_arp),
    .drum_patterns = s_hackfest_drums, .drum_pattern_count = COUNT(s_hackfest_drums),
    .bass_patterns = s_hackfest_bass, .bass_pattern_count = COUNT(s_hackfest_bass),
    .bass  = {.osc = SE_OSC_TRIANGLE, .osc_count = 1, .env = {0.002f, 0.10f, 0.7f, 0.04f}, .gain = 0.20f},
    .arp   = {.osc = SE_OSC_SQUARE, .osc_count = 1, .env = {0.001f, 0.06f, 0.3f, 0.04f},
              .filter = SE_FILTER_LPF, .cutoff_hz = 2800.0f, .q = 0.8f, .gain = 0.18f},
    .pad   = {.osc = SE_OSC_TRIANGLE, .osc_count = 1, .env = {0.25f, 0.4f, 0.55f, 0.6f},
              .gain = 0.30f / 3.0f, .amp_lfo_hz = 0.25f, .amp_lfo_depth = 0.15f},
    .kick  = {.osc = SE_OSC_SINE, .osc_count = 1, .env = {0.001f, 0.07f, 0.0f, 0.001f}, .gain = 0.24f,
              .pitch_env_amt = 70.0f},
    .snare = {.osc = SE_OSC_NOISE, .osc_count = 1, .env = {0.001f, 0.06f, 0.0f, 0.002f},
              .filter = SE_FILTER_BPF, .cutoff_hz = 2200.0f, .q = 0.9f, .gain = 0.14f},
    .hat   = {.osc = SE_OSC_NOISE, .osc_count = 1, .env = {0.001f, 0.025f, 0.0f, 0.001f},
              .filter = SE_FILTER_HPF, .cutoff_hz = 7000.0f, .q = 0.8f, .gain = 0.07f},
    .pad_detune = 0.003f,
};
// clang-format on

// "overtime": After hours. The night shift: closest to the game's own --
// minor, synthwave chords -- but deeper and moving, 80-86 BPM, a filtered
// saw bass in eighths, a detuned saw arp, a four-to-the-floor kick, a
// soft clap and offbeat hats as play gets going.
static se_music_progression_t const s_overtime_prog[] = {
    {{MI(0), MA(10), MA(8), MA(10), MI(0), MA(10), MA(8), MA(10)}},  // i VII VI VII
    {{MI(0), MA(8), MA(3), MA(10), MI(0), MA(8), MA(3), MA(10)}},    // i VI III VII
    {{MI(0), MI(5), MA(10), MA(3), MI(0), MI(5), MA(10), MA(3)}},    // i iv VII III
    {{MA(8), MA(10), MI(0), MI(0), MA(8), MA(10), MI(0), MI(0)}},    // VI VII i i
    {{MI(0), MA(8), MI(5), MA(7), MI(0), MA(8), MI(5), MA(7)}},      // i VI iv V
};
static se_music_arp_pattern_t const s_overtime_arp[] = {
    {{0, 2, 1, 2, 3, 2, 1, 2, 0, 2, 1, 2, 3, 2, 1, 2}},          // up and down
    {{0, -1, 1, -1, 2, -1, 3, -1, 4, -1, 3, -1, 2, -1, 1, -1}},  // climbing
    {{0, -1, -1, 2, 1, -1, 3, -1, 0, -1, 2, -1, -1, 3, 1, -1}},  // syncopated
    {{0, -1, 3, -1, 2, -1, 3, -1, 1, -1, 4, -1, 2, -1, 4, -1}},  // octaves
};
static se_music_drum_pattern_t const s_overtime_drums[] = {
    {.kick = 0x1111, .snare = 0x1010, .hat = 0x4444},  // four to the floor
    {.kick = 0x0101, .snare = 0x1010, .hat = 0x4444},  // half of it
    {.kick = 0x1111, .snare = 0x0000, .hat = 0x4444},
};
// clang-format off
static uint16_t const    s_overtime_bass[] = {0x5555, 0x5555, 0x1111, 0x5151};
static int8_t const      s_overtime_keys[] = {45, 47, 48, 50};  // A2, B2, C3, D3
static se_music_config_t const s_overtime = {
    .bpm_min = 80, .bpm_span = 7,
    .tonics = s_overtime_keys, .tonic_count = COUNT(s_overtime_keys),
    .progressions = s_overtime_prog, .progression_count = COUNT(s_overtime_prog),
    .arp_patterns = s_overtime_arp, .arp_pattern_count = COUNT(s_overtime_arp),
    .drum_patterns = s_overtime_drums, .drum_pattern_count = COUNT(s_overtime_drums),
    .bass_patterns = s_overtime_bass, .bass_pattern_count = COUNT(s_overtime_bass),
    .bass  = {.osc = SE_OSC_SAW, .osc_count = 1, .env = {0.004f, 0.14f, 0.5f, 0.05f},
              .filter = SE_FILTER_LPF, .cutoff_hz = 420.0f, .q = 1.0f, .gain = 0.26f},
    .arp   = {.osc = SE_OSC_SAW, .osc_count = 2, .detune = 0.006f, .env = {0.003f, 0.16f, 0.12f, 0.25f},
              .filter = SE_FILTER_LPF, .cutoff_hz = 1500.0f, .q = 1.2f, .gain = 0.13f},
    .pad   = {.osc = SE_OSC_SAW, .osc_count = 1, .env = {0.6f, 0.4f, 0.7f, 1.5f},
              .filter = SE_FILTER_LPF, .cutoff_hz = 1000.0f, .q = 0.7f, .gain = 0.17f / 3.0f,
              .amp_lfo_hz = 0.12f, .amp_lfo_depth = 0.25f},
    .kick  = {.osc = SE_OSC_SINE, .osc_count = 1, .env = {0.001f, 0.10f, 0.0f, 0.001f}, .gain = 0.30f,
              .pitch_env_amt = 55.0f},
    .snare = {.osc = SE_OSC_NOISE, .osc_count = 1, .env = {0.002f, 0.11f, 0.0f, 0.002f},
              .filter = SE_FILTER_BPF, .cutoff_hz = 1400.0f, .q = 0.9f, .gain = 0.12f},
    .hat   = {.osc = SE_OSC_NOISE, .osc_count = 1, .env = {0.001f, 0.03f, 0.0f, 0.001f},
              .filter = SE_FILTER_HPF, .cutoff_hz = 6500.0f, .q = 0.8f, .gain = 0.06f},
    .pad_detune = 0.006f,
};
// clang-format on

// Each tune's layers -- the roles the mood brings in and out -- by their
// level at energy 0 (a desk, between chambers) and 1 (a turret has seen
// you); in play the energy is the tune's `play`, on an outro's walk its
// `outro`. The pad always plays. The game's own has none of it.
enum {
    L_BASS,
    L_ARP,
    L_KICK,
    L_SNARE,
    L_HAT,
    L_N
};

typedef struct {
    char const*              name;
    se_music_config_t const* cfg;  // NULL: the game's own, s_music
    uint32_t                 seed;
    float                    lo[L_N], hi[L_N];
    float                    play, outro;
} tune_t;

// clang-format off
static tune_t const s_tunes[] = {
    {"calm", NULL, MUSIC_SEED, {0}, {0}, 0.0f, 0.0f},
    //                                bass   arp   kick  tock  tick
    {"office", &s_office, 0x0FF1CEu, {0.55f, 1.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.8f, 1.4f, 0.15f, 0.15f}, 0.7f, 0.0f},
    //                                     bass  arp   kick  snare hat
    {"hackfest", &s_hackfest, 0x4AC4F357u, {0.6f, 1.0f, 0.0f, 0.0f, 0.6f}, {1.0f, 1.0f, 1.2f, 1.3f, 1.4f}, 0.7f, 0.85f},
    {"overtime", &s_overtime, 0x0B5E55EDu, {0.7f, 0.9f, 0.25f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.2f, 1.3f, 1.4f}, 0.7f, 0.0f},
};
// clang-format on

// A pack's `music:` value: a tune's name, then any of bpm=N (a tempo of
// its own, 40-180), key=X (its tonic: C, C#, Db ... B) and seed=N (other
// music from the same tune). "" or an unknown name: the game's own.
typedef struct {
    tune_t const* tune;
    int           bpm;   // 0: the tune's
    int           key;   // a MIDI tonic, or -1: the tune's
    uint32_t      seed;  // the tune's, unless seed= says
} spec_t;

static bool word_is(char const* w, size_t n, char const* name) {
    return strlen(name) == n && strncmp(w, name, n) == 0;
}

static void spec_parse(char const* text, spec_t* s) {
    s->tune    = &s_tunes[0];
    s->bpm     = 0;
    s->key     = -1;
    s->seed    = s_tunes[0].seed;
    bool named = false;
    for (char const* p = text != NULL ? text : ""; *p;) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        char const* const w = p;
        while (*p && *p != ' ' && *p != '\t' && *p != ',') p++;
        size_t const n = (size_t)(p - w);
        if (n == 0) break;
        char const* const eq = memchr(w, '=', n);
        if (eq == NULL && !named) {
            named = true;
            for (int i = 0; i < COUNT(s_tunes); i++)
                if (word_is(w, n, s_tunes[i].name)) s->tune = &s_tunes[i];
            s->seed = s->tune->seed;
        } else if (eq != NULL && word_is(w, (size_t)(eq - w), "bpm")) {
            int const bpm = atoi(eq + 1);
            if (bpm >= 40 && bpm <= 180) s->bpm = bpm;
        } else if (eq != NULL && word_is(w, (size_t)(eq - w), "seed")) {
            s->seed = (uint32_t)strtoul(eq + 1, NULL, 0);
        } else if (eq != NULL && word_is(w, (size_t)(eq - w), "key")) {
            static char const   letters[] = "CDEFGAB";
            static int8_t const semis[]   = {0, 2, 4, 5, 7, 9, 11};
            char const          c         = (char)(eq[1] & ~0x20);  // upper case
            char const* const   at        = c ? strchr(letters, c) : NULL;
            if (at != NULL) {
                int k = semis[at - letters];
                if (eq + 2 < p && eq[2] == '#') k++;
                if (eq + 2 < p && eq[2] == 'b') k--;
                s->key = 45 + (k - 9 + 24) % 12;  // A2 .. G#3, as the tunes' own keys
            }
        }
    }
}

static bool spec_same(spec_t const* a, spec_t const* b) {
    return a->tune == b->tune && a->bpm == b->bpm && a->key == b->key && a->seed == b->seed;
}

// --- A band: one tune, playing ---------------------------------------------
//
// The generator with the tune's config, kept with it (the generator keeps
// a pointer), and its layers: the built-in voice, struck at the level the
// mood gives it -- set on the mixer's task before each block, read there
// at a note's start, so a layer comes and goes a note at a time and costs
// nothing a sample.

typedef struct {
    se_voice_t       base;  // first
    se_voice_synth_t syn;
    float const*     level;
} layer_t;

typedef struct {
    tune_t const*     tune;
    music_source_t*   src;
    se_music_config_t cfg;
    int8_t            key;
    float             level[L_N];
    layer_t           layer[L_N];
} band_t;

static void layer_on(se_voice_t* v, float hz, float vel) {
    layer_t* const l = (layer_t*)v;
    if (*l->level > 0.01f) l->syn.base.note_on(&l->syn.base, hz, vel * *l->level);
}
static void layer_off(se_voice_t* v) {
    layer_t* const l = (layer_t*)v;
    if (l->syn.base.note_off != NULL) l->syn.base.note_off(&l->syn.base);
}
static void layer_render(se_voice_t* v, float* mix, size_t frames) {
    layer_t* const l = (layer_t*)v;
    l->syn.base.render(&l->syn.base, mix, frames);
}
static bool layer_active(se_voice_t const* v) {
    layer_t const* const l = (layer_t const*)v;
    return l->syn.base.active == NULL || l->syn.base.active(&l->syn.base);
}

static void band_free(band_t* b) {
    if (b == NULL) return;
    if (b->src != NULL && b->src->shutdown != NULL) b->src->shutdown(b->src);
    free(b);
}

static band_t* band_new(spec_t const* s) {
    band_t* const b = calloc(1, sizeof(*b));
    if (b == NULL) return NULL;
    tune_t const* const t = s->tune;
    b->tune               = t;
    b->cfg                = t->cfg != NULL ? *t->cfg : s_music;
    if (s->bpm > 0) {
        b->cfg.bpm_min  = (uint16_t)s->bpm;
        b->cfg.bpm_span = 1;
    }
    if (s->key >= 0) {
        b->key             = (int8_t)s->key;
        b->cfg.tonics      = &b->key;
        b->cfg.tonic_count = 1;
    }
    if (t->cfg != NULL) {
        se_voice_spec_t const* const spec[L_N] = {&b->cfg.bass, &b->cfg.arp, &b->cfg.kick, &b->cfg.snare, &b->cfg.hat};
        se_voice_t** const role[L_N] = {&b->cfg.bass_voice, &b->cfg.arp_voice, &b->cfg.kick_voice, &b->cfg.snare_voice,
                                        &b->cfg.hat_voice};
        for (int i = 0; i < L_N; i++) {
            layer_t* const l = &b->layer[i];
            se_voice_synth_init(&l->syn, spec[i]);
            l->base  = (se_voice_t){layer_on, layer_off, layer_render, layer_active};
            l->level = &b->level[i];
            *role[i] = &l->base;
        }
    }
    b->src = music_procedural_create(&b->cfg, s->seed);
    if (b->src == NULL) {
        free(b);
        return NULL;
    }
    return b;
}

// --- The mix: the tune, faded in over the last, and the mood ---------------
//
// What the mixer holds (se_audio.h): it renders it on its own task (core
// 1, under its lock), never the frame's. A new tune is a new mix, the band
// that was playing handed on to fade out under it; the mixer swaps it in
// under its lock and shuts the last mix down, on the game's task -- the
// only place anything is freed. Everything the game asks of the mood is a
// plain word written here and read there, a chunk late at worst.
//
// The game's own tune, alone, renders straight into the mixer's buffer:
// sample for sample as before there were tunes.

#define MIX_CHUNK 256
#define FADE_S    3.0f     // a new tune over the last
#define OPEN_HZ   9000.0f  // the low-pass, open: passed by
#define DUCK      0.7f     // under GLaDOS
#define ALERT_S   5.0f     // a turret has seen you: tense, this long
#define DIP_HZ    220.0f   // a death: all but the bass gone ...
#define DIP_S     1.2f     // ... and back over this long
#define E_SET     0        // a mood's energy: its own
#define E_PLAY    1        // ... the tune's `play`
#define E_OUTRO   2        // ... the tune's `outro`

typedef struct {
    uint8_t from;  // the energy: its own (E_SET), the tune's `play` or `outro`
    float   energy;
    float   cut;  // the low-pass, Hz
    float   gain;
} mood_t;

// clang-format off
static mood_t const s_moods[] = {
    [SOUND_MOOD_DESK]  = {E_SET, 0.0f, OPEN_HZ, 1.0f},
    [SOUND_MOOD_PLAY]  = {E_PLAY, 0.0f, OPEN_HZ, 1.0f},
    [SOUND_MOOD_PAUSE] = {E_SET, 0.0f, 1400.0f, 0.85f},   // the menu over play
    [SOUND_MOOD_RIDE]  = {E_SET, 0.3f, 600.0f, 0.9f},     // through the lift's walls
    [SOUND_MOOD_WALK]  = {E_OUTRO, 0.0f, 2400.0f, 0.9f},  // the lights low
    [SOUND_MOOD_DARK]  = {E_SET, 0.0f, 350.0f, 0.55f},    // ... and out
};
// clang-format on

// Written by the game, read by the mixer.
static volatile int      s_mood = SOUND_MOOD_DESK;
static volatile bool     s_ducked;
static volatile unsigned s_alerts, s_dips;

typedef struct {
    music_source_t base;          // first
    band_t*        cur;           // the tune
    band_t*        old;           // the last, fading out under it; NULL for none
    bool           handed;        // cur went on to the next mix, to fade out there
    uint32_t       fade, fade_n;  // samples into the fade, of fade_n
    // The mood as it stands, eased towards what the game asks:
    float          energy, cut, gain, dip;
    float          y1, y2;        // the low-pass: two one-pole stages, 12 dB an octave
    uint32_t       alert;         // samples of tension left
    unsigned       alerts, dips;  // the requests seen
} mix_t;

static mix_t* s_mix;            // the mix the mixer holds
static spec_t s_spec;           // its tune
static char   s_spec_text[64];  // ... as asked for
static bool   s_gate = true;    // music audible (Settings)
static bool   s_fresh;          // restarted this frame: a new tune starts, not fades in

static void band_levels(band_t* b, float e) {
    if (b == NULL || b->tune->cfg == NULL) return;
    for (int i = 0; i < L_N; i++) b->level[i] = b->tune->lo[i] + (b->tune->hi[i] - b->tune->lo[i]) * e;
}

static float ease(float x, float to, float rate) {  // a linear slew
    return x < to ? fminf(to, x + rate) : fmaxf(to, x - rate);
}

static void mix_chunk(mix_t* m, int16_t* out, size_t n) {
    static int16_t      old[2 * MIX_CHUNK];
    float const         dt    = (float)n / SR;
    tune_t const* const t     = m->cur->tune;
    bool const          moody = t->cfg != NULL;  // the game's own has no moods
    int const           mood  = s_mood;
    mood_t const        md    = s_moods[mood >= 0 && mood < COUNT(s_moods) ? mood : 0];
    // What the game asks.
    float               e_to  = md.from == E_PLAY ? t->play : md.from == E_OUTRO ? t->outro : md.energy;
    float               cut   = moody ? md.cut : OPEN_HZ;
    float               gain  = moody ? md.gain * (s_ducked ? DUCK : 1.0f) : 1.0f;
    if (s_alerts != m->alerts) {
        m->alerts = s_alerts;
        if (moody && mood == SOUND_MOOD_PLAY) m->alert = (uint32_t)(ALERT_S * SR);
    }
    if (s_dips != m->dips) {
        m->dips = s_dips;
        if (moody) m->dip = 1.0f;
    }
    bool const tense = m->alert > 0 && mood == SOUND_MOOD_PLAY;
    if (tense) e_to = 1.0f;
    m->alert        = m->alert > n ? m->alert - (uint32_t)n : 0;
    // Eased: the layers over a couple of seconds (tension in half of one),
    // the filter and the level in a few tenths.
    m->energy       = ease(m->energy, e_to, (tense ? 2.0f : m->energy < e_to ? 0.5f : 0.35f) * dt);
    m->cut         *= powf(cut / m->cut, 1.0f - expf(-dt / 0.35f));
    m->dip          = fmaxf(0.0f, m->dip - dt / DIP_S);
    float const g0  = m->gain;
    m->gain        += (gain - m->gain) * (1.0f - expf(-dt / 0.3f));
    band_levels(m->cur, m->energy);
    band_levels(m->old, m->energy);

    m->cur->src->render(m->cur->src, out, n);
    bool const fading = m->old != NULL && m->fade < m->fade_n;
    if (fading) {
        memset(old, 0, sizeof(old[0]) * 2 * n);
        m->old->src->render(m->old->src, old, n);
    }
    // The low-pass's cutoff, dipped by a death; open, passed by.
    float const fc   = m->dip > 0.0f ? m->cut * powf(DIP_HZ / m->cut, m->dip * m->dip) : m->cut;
    bool const  open = fc >= OPEN_HZ * 0.98f;
    float const a    = 1.0f - expf(-2.0f * 3.14159265f * fc / SR);
    // The fade, equal power: sin in, cos out, eased within the chunk.
    float const x0   = fading ? (float)m->fade / (float)m->fade_n : 1.0f;
    m->fade          = fading ? m->fade + (uint32_t)n : m->fade;
    float const x1   = fading ? fminf(1.0f, (float)m->fade / (float)m->fade_n) : 1.0f;
    float const i0 = sinf(x0 * 1.5707963f), i1 = sinf(x1 * 1.5707963f);
    float const o0 = cosf(x0 * 1.5707963f), o1 = cosf(x1 * 1.5707963f);
    float const k = 1.0f / (float)n;
    for (size_t i = 0; i < n; i++) {
        float const f = (float)i * k;
        float       s = (float)out[2 * i] * (i0 + (i1 - i0) * f);
        if (fading) s += (float)old[2 * i] * (o0 + (o1 - o0) * f);
        if (open) {
            m->y1 = m->y2 = s;
        } else {
            m->y1 += a * (s - m->y1);
            m->y2 += a * (m->y1 - m->y2);
            s      = m->y2;
        }
        s               *= g0 + (m->gain - g0) * f;
        int16_t const o  = (int16_t)(s > 32767.0f ? 32767.0f : s < -32768.0f ? -32768.0f : s);
        out[2 * i]       = o;
        out[2 * i + 1]   = o;
    }
}

static void mix_render(music_source_t* self, int16_t* out, size_t frames) {
    mix_t* const m = (mix_t*)self;
    if (m->cur->tune->cfg == NULL && (m->old == NULL || m->fade >= m->fade_n)) {
        // The game's own, alone: as it always played.
        m->cur->src->render(m->cur->src, out, frames);
        return;
    }
    for (size_t done = 0; done < frames;) {
        size_t const n = frames - done < MIX_CHUNK ? frames - done : MIX_CHUNK;
        mix_chunk(m, out + 2 * done, n);
        done += n;
    }
}

static void mix_shutdown(music_source_t* self) {
    mix_t* const m = (mix_t*)self;
    band_free(m->old);
    if (!m->handed) band_free(m->cur);
    free(m);
}

// A new mix of tune s_spec: faded in over the one playing, or from cold.
static void music_start(bool fade) {
    mix_t* const  m = calloc(1, sizeof(*m));
    band_t* const b = band_new(&s_spec);
    if (m == NULL || b == NULL) {
        free(m);
        band_free(b);
        return;
    }
    m->base.render    = mix_render;
    m->base.shutdown  = mix_shutdown;
    m->cur            = b;
    mix_t* const prev = s_mix;
    if (prev != NULL) {
        // The mood goes on where it stood (read as the mixer left it).
        m->energy = prev->energy;
        m->cut    = prev->cut;
        m->gain   = prev->gain;
        m->dip    = prev->dip;
        m->y1     = prev->y1;
        m->y2     = prev->y2;
        m->alert  = prev->alert;
    } else {
        m->cut  = OPEN_HZ;
        m->gain = 1.0f;
    }
    m->alerts = s_alerts;
    m->dips   = s_dips;
    if (fade && prev != NULL) {
        m->old       = prev->cur;
        prev->handed = true;
        m->fade_n    = (uint32_t)(FADE_S * SR);
    }
    // The mixer swaps it in under its lock and shuts the last one down.
    audio_mixer_set_music(&m->base);
    s_mix = m;
}

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
    s_music.arp.env.release     = 0.6f;
    s_music.bass.filter         = SE_FILTER_LPF;
    s_music.bass.cutoff_hz      = 700.0f;
    s_music.pad.gain           *= 1.3f;
    s_music.pad.amp_lfo_hz      = 0.15f;
    s_music.pad.amp_lfo_depth   = 0.3f;
    s_music.kick.gain          *= 0.6f;
    s_music.snare_voice         = NULL;
    s_music.hat_voice           = NULL;
    spec_parse(NULL, &s_spec);
    s_spec_text[0] = '\0';
    s_mood         = SOUND_MOOD_DESK;
    music_start(false);
    audio_mixer_set_music_volume(100);
}

void sound_restart_music(void) {
    // The tune from its first bar: a new mix, nothing to fade.
    music_start(false);
    s_fresh = true;
}

void sound_music_tune(char const* spec) {
    if (spec == NULL) spec = "";
    if (strcmp(spec, s_spec_text) == 0) return;
    snprintf(s_spec_text, sizeof(s_spec_text), "%s", spec);
    spec_t s;
    spec_parse(spec, &s);
    if (spec_same(&s, &s_spec)) return;
    s_spec = s;
    // Faded in over the last, if that was being heard and not just begun.
    music_start(s_gate && !s_fresh);
}

void sound_music_mood(sound_mood_t mood) {
    s_mood = (int)mood;
}

static void music_events(int ev) {
    if (ev & PL_EV_DIED) s_dips++;
    if (ev & GAME_EV_SPOTTED) s_alerts++;
}

static void music_frame(void) {
    s_ducked = sound_saying();
    s_fresh  = false;
}

void sound_set_music(bool on) {
    s_gate = on;
    audio_mixer_set_music_enabled(on);
}

void sound_set_effects(bool on) {
    s_fx_on = on;
    audio_mixer_set_group_enabled(GROUP_FX, on);
}
