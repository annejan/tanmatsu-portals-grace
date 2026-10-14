// Host movie: demos' runs, as frames and sound, for tools/make_movie.py.
//
//   host_movie SECONDS DEMO [DEMO...]
//
// films the demos one after the other, as the game goes from chamber to
// chamber, each at most SECONDS long: it writes $BUILD/shots/movie_N.ppm
// at 10 frames a second, $BUILD/movie.wav and $BUILD/movie.txt (below).
//
// HOST_MOVIE_LIFTS=1: from chamber to chamber by lift, as the game goes
// with Settings -> Lifts. HOST_MOVIE_BADGE=1 (lifts too): only what the
// badge itself shows -- no titles; the first chamber arrived at by lift,
// as after Continue; the film over at the top of the last ride up; and on
// every picture the game's own HUD (main/hud.c, tests/movie_hud.c) and
// the lift's fade in RGB565, as main.c's on_render draws them, less the
// frame rate line.
//
// HOST_MOVIE_STORY=FILE (with HOST_MOVIE_BADGE): the demos are a desk
// story's rounds and its outro, filmed as main.c plays them -- no lifts,
// as in a desk story. FILE has a line per demo (tools/story_film.py
// writes it from host_review's day):
//
//   R <TAB> round <TAB> drafts <TAB> fresh <TAB> broken <TAB> badge <TAB> headline <TAB> line
//       a round: main.c's play_chamber -- its message ("Round N: name",
//       or the name on a badge-style desk; a broken draft's play-test
//       says start_playtest's), its story told only when `fresh` (a new
//       file's) -- then at the exit round_exit: `headline` the message,
//       `line` GLaDOS's, said and typed, the game stopped until both are
//       done (main.c's s_desk_return), and the film cuts to the desk
//   O <TAB> PACK_DIR
//       the outro (main/outro.c, MODE_CINE): the walk, lights low, the
//       pack's ending said and typed; the fade; then the desk's cards
//
// HOST_MOVIE_DESKS=N,N,...: before each demo, so many frames of a desk
// story's desk (tools/make_movie.py --badge splices in tests/host_desk.c's
// pictures there): no pictures here, but the sound going on as main.c's
// MODE_DESK has it -- the tune in the desk's mood, nothing else playing.
//
// HOST_MOVIE_MUSIC: the tune, a pack's `music:` (sound.h; tools/dlc.py and
// story_film.py pass the pack's), played to the mood as main.c plays it.
// HOST_MOVIE_STEM=music: the music alone in movie.wav.
// HOST_MOVIE_PROGRESS=P,P,...: how far into its story each demo is, 0..1,
// as main.c tells sound_music_progress (the desk before it too); unset,
// each demo's place among them: the first 0, the last 1.
//
// HOST_MOVIE_NICK: the badge owner's nickname for "[Subject-Name-here]",
// as main.c's personalise; unset, the joke stands as written.
// The frames come from host_shot's rasterizer, included below, through a
// camera of their own (below); the sound is the game's own -- sound.c,
// speech.c with SAM, the engine's music -- mixed here offline as the
// badge's mixer mixes it, on one clock, so the music plays on from
// chamber to chamber.

#define main host_shot_main
#include "host_shot.c"
#undef main

#include "cine.h"
#include "desk.h"
#include "hud.h"
#include "lift.h"
#include "movie_hud.h"
#include "outro.h"
#include "pack.h"
#include "sound.h"
#include "story.h"

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

// The badge's mixer fills the engine's sine table as it starts: without it
// audio_dsp_sin() is 0, and every sine -- the music's arp and kick, the
// effects' -- silent.
esp_err_t audio_mixer_init(void) {
    return audio_dsp_init();
}
void audio_mixer_set_music(music_source_t* src) {
    music_source_t* const prev = s_music;
    s_music                    = src;
    if (prev != NULL && prev->shutdown != NULL) prev->shutdown(prev);  // as the badge's mixer
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
    // HOST_MOVIE_STEM=music: the music alone, the effects and voices
    // played but not heard -- to hold the music up against another build's.
    static int music_only = -1;
    if (music_only < 0) music_only = getenv("HOST_MOVIE_STEM") != NULL && strcmp(getenv("HOST_MOVIE_STEM"), "music") == 0;
    for (int i = 0; i < SLOTS; i++) {
        sfx_voice_t* v = s_voices[i];
        if (v == NULL) continue;
        if (!v->finished && (v->group >= SE_AUDIO_SFX_GROUP_COUNT || s_group_on[v->group])) {
            memset(one, 0, sizeof(one));
            v->render(v, one, frames);
            if (!music_only)
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
// $BUILD/movie.txt has a line per frame of film -- "C" the opening titles
// (none with HOST_MOVIE_BADGE),
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
    bool        lifts;   // HOST_MOVIE_LIFTS: from chamber to chamber by lift (lift.h), as the game goes
    bool        badge;   // HOST_MOVIE_BADGE: what the badge shows, its HUD drawn on the pictures (above)
    bool        round;   // HOST_MOVIE_STORY: this run is a desk story's round
    char const* headline;  // ... what the game says at its exit
    char const* line;      // ... and GLaDOS
    char const* said;      // her line, once the exit is reached: the story line's place
} rec_t;

// With lifts: the chamber's stations, drawn all along, and the game as it
// stood the moment it reached the exit -- the ride up starts from there.
static lift_sites_t s_sites;
static game_t       s_exit;
static bool         s_exit_have;
static lift_depart_t s_depart;  // the start's car, going back up once the player is out

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
        sound_music_mood(SOUND_MOOD_DESK);
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
    // With lifts, filming stops on the exit: the ride takes it from there;
    // a round's, the verdict (film_verdict).
    if ((r->lifts || r->round) && r->exit_t >= 0.0f && now > r->exit_t) return;
    // Moving on, but not over GLaDOS: a chamber solved in seconds would cut
    // her line off with the next one's. A TAS moves on, as the game does.
    if (r->exit_t >= 0.0f && now > r->exit_t + (r->tas ? TAS_HOLD : MESSAGE_S) &&
        (r->tas || !sound_saying() || now > r->exit_t + 15.0f))
        return;
    r->last = now;
    // Every step, not only those filmed: the start's car goes at its own
    // pace, and the exit is reached on a step between two pictures as
    // often as not.
    if (r->lifts) {
        int const dev = lift_depart_step(&s_depart, &s_sites.start, g->pl.pos, TICK);
        if (dev & LIFT_EV_DOOR) sound_play(SND_DOOR);
        if (dev & LIFT_EV_RIDE) sound_play(SND_HUM);
    }
    if ((r->lifts || r->round) && (ev & PL_EV_EXIT)) {
        s_exit          = *g;  // the ride begins as the camera saw it
        s_exit.pl.yaw   = k < s_n ? s_cam[k][0] : g->pl.yaw;
        s_exit.pl.pitch = k < s_n ? s_cam[k][1] : g->pl.pitch;
        s_exit_have     = true;
    }
    sound_events(ev);
    sound_update();
    // main.c's music_mood: play, and calm once the exit is reached -- a
    // verdict, "Chamber complete".
    sound_music_mood(r->exit_t >= 0.0f ? SOUND_MOOD_DESK : SOUND_MOOD_PLAY);
    float const film = r->t0 + now;
    sound_to(r, film);
    if (r->badge) {
        // main.c's play frame: the HUD's clocks, and at the exit its word.
        hud_tick(TICK);
        if ((ev & PL_EV_EXIT) && r->round) {
            // main.c's round_exit: the verdict, and GLaDOS's line said and
            // typed in the story line's place.
            hud_message(r->headline);
            r->said = r->line;
            hud_story_start();
            sound_say(r->line);
        } else if (ev & PL_EV_EXIT) {
            hud_message(r->done);
        }
    }

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
    snprintf(shot.lv.story, sizeof(shot.lv.story), "%s", r->said != NULL ? r->said : r->story);
    for (int p = 0; p < W * H; p++) s_px[p] = 0xFF000000u;
    if (r->lifts) {
        lift_view_t v;
        lift_view(NULL, &s_sites, &s_depart, &v);
        render_set_lift(&v);
    }
    render_set_level(&shot.lv, shot.portals);
    render_set_time(now);
    render_frame(NULL, &shot);
    if (r->badge) movie_hud_frame(s_px, 1.0f, &shot);
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

// A ride, up out of the chamber in `g` (from its exit) or down into it
// (onto its start), filmed: the lift's own sounds, the screen dark at the
// top of the shaft, the HUD saying `msg` with the run's clock at `time`.
// Up, it waits shut while GLaDOS finishes what she is saying -- with
// HOST_MOVIE_BADGE as main.c's does: shut while the message is up after
// the `last` chamber, else straight on.
static void film_ride(rec_t* r, game_t* g, lift_t* l, char const* msg, float time, float exit_t, bool last) {
    float t = 0.0f;
    for (int k = 0; k < 4000 && lift_on(l); k++) {
        bool const talking = r->badge ? last && hud_message_up() : sound_saying() && t < 15.0f;
        if (r->badge) hud_tick(TICK);  // main.c's play frame: before the lift's step
        if (l->car_exit) {
            // Up out of the chamber: the start's car still on its way up, as
            // main.c steps it, before the lift.
            int const dev = lift_depart_step(&s_depart, &s_sites.start, g->pl.pos, TICK);
            if (dev & LIFT_EV_DOOR) sound_play(SND_DOOR);
            if (dev & LIFT_EV_RIDE) sound_play(SND_HUM);
        }
        int const ev = lift_step(l, g, TICK, l->phase == LIFT_HOLD && talking, 0.0f, 0.0f, r->tas ? 1.5f : 1.0f);
        if (ev & LIFT_EV_FIZZLE) sound_play(SND_FIZZLE);
        if (ev & LIFT_EV_DOOR) sound_play(SND_DOOR);
        if (ev & LIFT_EV_RIDE) sound_play(SND_HUM);
        if (ev & LIFT_EV_LAND) {
            sound_play(SND_LAND);
            sound_play(SND_DING);
        }
        t += TICK;
        sound_update();
        sound_music_mood(l->phase == LIFT_UP || l->phase == LIFT_DOWN ? SOUND_MOOD_RIDE : SOUND_MOOD_PLAY);
        float const film = r->t0 + t;
        sound_to(r, film);
        if (film + 1e-4f >= (float)r->frame / FPS) {
            lift_view_t v;
            lift_view(l, &s_sites, &s_depart, &v);
            render_set_lift(&v);
            for (int p = 0; p < W * H; p++) s_px[p] = 0xFF000000u;
            render_set_level(&g->lv, g->portals);
            render_frame(NULL, g);
            // Dark at the top of the shaft: on the badge, hud_fade's, with
            // the HUD over it.
            uint32_t const lit = (uint32_t)(256.0f * lift_lit(l));
            if (r->badge) {
                movie_hud_frame(s_px, lift_lit(l), g);
            } else {
                for (int p = 0; p < W * H; p++) {
                    uint32_t const c = s_px[p];
                    s_px[p] = 0xFF000000u | (((c >> 16 & 0xFFu) * lit >> 8) << 16) |
                              (((c >> 8 & 0xFFu) * lit >> 8) << 8) | ((c & 0xFFu) * lit >> 8);
                }
            }
            char name[32];
            snprintf(name, sizeof(name), "movie_%05d", r->pic);
            save(name);
            fprintf(r->txt, "P\t%d\t-1\t\t%s\t%.2f\t%.2f\n", r->pic, msg, (double)time, (double)exit_t);
            r->pic++;
            r->frame++;
        }
        if (ev & LIFT_EV_TOP) break;  // up: the next chamber takes it from here
    }
    r->t0 += t;
}

// `n` frames at a desk story's desk (HOST_MOVIE_DESKS): main.c's MODE_DESK
// frames -- sound_update, desk_frame (no sound of its own: the desk and
// deskview.c play none), music_where in the desk's mood. The pictures are
// make_movie.py's to splice in; here the clock and the frame count go on
// by them, so the frames after fall where they would with none.
static void film_desk(rec_t* r, int n) {
    float const from = r->t0, len = (float)n / FPS;
    for (float t = 0.0f; t < len - 1e-4f;) {
        t = fminf(t + TICK, len);
        sound_update();
        sound_music_mood(SOUND_MOOD_DESK);
        sound_to(r, from + t);
    }
    r->t0    += len;
    r->frame += n;
}

// A round's exit, as main.c's play frame goes on after round_exit: the
// game stopped where it stood, the verdict up and GLaDOS saying her line;
// once the message is down and she is done, back to the desk -- a cut.
static void film_verdict(rec_t* r) {
    static game_t shot;
    shot = s_exit;  // the camera as it saw the exit
    snprintf(shot.lv.story, sizeof(shot.lv.story), "%s", r->said != NULL ? r->said : "");
    float t = 0.0f;
    for (int k = 0; k < 60 * 50; k++) {
        sound_update();
        sound_music_mood(SOUND_MOOD_DESK);
        hud_tick(TICK);
        if (!hud_message_up() && !sound_saying()) break;
        t += TICK;
        float const film = r->t0 + t;
        sound_to(r, film);
        if (film + 1e-4f < (float)r->frame / FPS) continue;
        for (int p = 0; p < W * H; p++) s_px[p] = 0xFF000000u;
        render_set_level(&shot.lv, shot.portals);
        render_set_time(r->exit_t + t);
        render_frame(NULL, &shot);
        movie_hud_frame(s_px, 1.0f, &shot);
        char name[32];
        snprintf(name, sizeof(name), "movie_%05d", r->pic);
        save(name);
        fprintf(r->txt, "P\t%d\t-1\t\t%s\t%.2f\t%.2f\n", r->pic, r->headline, (double)(r->exit_t + t),
                (double)r->exit_t);
        r->pic++;
        r->frame++;
    }
    r->t0 += t;
}

// A desk story's outro, as main.c's MODE_CINE goes: DESK_LEAVE starts it
// (outro_start, the ending said and typed), cine_frame steps it, and
// on_render draws the walk faded by outro_lit with the HUD over it, then
// outro_draw's cards. Over: the title screen, and the film's end.
static void film_outro(rec_t* r, int chamber, desk_data_t const* d, char const* ending) {
    static game_t g;
    memset(&g, 0, sizeof(g));
    if (!outro_start(&g, chamber, d)) {
        fprintf(stderr, "the outro does not start: no solution?\n");
        exit(1);
    }
    snprintf(g.lv.story, sizeof(g.lv.story), "%s", ending);
    hud_quiet();
    hud_story_start();
    sound_say(ending[0] ? ending : NULL);
    render_set_level(&g.lv, g.portals);
    float t = 0.0f;
    for (int k = 0; k < 400 * 50; k++) {
        sound_update();
        hud_tick(TICK);
        int ev = 0;
        if (!outro_update(&g, TICK, &ev)) break;
        sound_events(ev);
        sound_music_mood(!outro_world()    ? SOUND_MOOD_DESK
                         : outro_walking() ? SOUND_MOOD_WALK
                                           : SOUND_MOOD_DARK);
        if (ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) render_set_level(&g.lv, g.portals);
        t += TICK;
        float const film = r->t0 + t;
        sound_to(r, film);
        if (film + 1e-4f < (float)r->frame / FPS) continue;
        if (outro_world()) {
            for (int p = 0; p < W * H; p++) s_px[p] = 0xFF000000u;
            render_set_level(&g.lv, g.portals);
            render_set_time(t);
            render_frame(NULL, &g);
            movie_hud_frame(s_px, outro_lit(), &g);
        } else {
            movie_outro_cards(s_px);
        }
        char name[32];
        snprintf(name, sizeof(name), "movie_%05d", r->pic);
        save(name);
        fprintf(r->txt, "P\t%d\t-1\t\t\t%.2f\t-1\n", r->pic, (double)t);
        r->pic++;
        r->frame++;
    }
    r->t0 += t;
}

// main.c's personalise: "[Subject-Name-here]" the owner's nickname, if
// there is one and it fits; otherwise the joke stands as written.
static void personalise(char* story, size_t n) {
    static char const token[] = "[Subject-Name-here]";
    char* const       at      = strstr(story, token);
    char const* const name    = getenv("HOST_MOVIE_NICK");
    if (at == NULL || name == NULL || name[0] == '\0') return;
    if (strlen(story) - strlen(token) + strlen(name) >= n) return;
    char out[320];
    snprintf(out, sizeof(out), "%.*s%s%s", (int)(at - story), story, name, at + strlen(token));
    snprintf(story, n, "%.*s", (int)n - 1, out);
}

// HOST_MOVIE_STORY's line for each demo (above).
typedef struct {
    char kind;  // 'R' a round, 'O' the outro, 0 neither
    int  round, drafts;
    bool fresh, broken, badge;
    char headline[64], line[STORY_LINE_N], pack[256];
} story_info_t;

static int read_story(char const* path, story_info_t* out, int max) {
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    char text[1024];
    int  n = 0;
    while (n < max && fgets(text, sizeof(text), f) != NULL) {
        text[strcspn(text, "\n")] = '\0';
        story_info_t* s = &out[n++];
        memset(s, 0, sizeof(*s));
        char* field[8];
        int   nf = 0;
        for (char* p = text; nf < 8;) {
            field[nf++] = p;
            char* const tab = strchr(p, '\t');
            if (tab == NULL) break;
            *tab = '\0';
            p    = tab + 1;
        }
        s->kind = field[0][0];
        if (s->kind == 'R' && nf == 8) {
            s->round  = atoi(field[1]);
            s->drafts = atoi(field[2]);
            s->fresh  = field[3][0] == '1';
            s->broken = field[4][0] == '1';
            s->badge  = field[5][0] == '1';
            snprintf(s->headline, sizeof(s->headline), "%s", field[6]);
            snprintf(s->line, sizeof(s->line), "%s", field[7]);
        } else if (s->kind == 'O' && nf == 2) {
            snprintf(s->pack, sizeof(s->pack), "%s", field[1]);
        } else {
            fprintf(stderr, "%s, line %d: not a round's or the outro's\n", path, n);
            exit(1);
        }
    }
    fclose(f);
    return n;
}

static char* slurp(char const* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long const n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* t = malloc((size_t)n + 1);
    if (t == NULL || fread(t, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(t);
        return NULL;
    }
    t[n] = '\0';
    fclose(f);
    return t;
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

    if (audio_mixer_init() != ESP_OK) {
        fprintf(stderr, "audio_dsp_init failed\n");
        return 1;
    }
    sound_init();
    sound_set_music(true);
    // HOST_MOVIE_MUSIC: a story pack's tune (pack.h `music:`), from its
    // first bar, as a story begun from the menu; unset, the game's own.
    sound_restart_music();
    sound_music_tune(getenv("HOST_MOVIE_MUSIC"));
    sound_set_effects(true);
    sound_set_voice(true);

    float const cap  = (float)atof(argv[1]);
    char const* tick = getenv("HOST_MOVIE_TICK");
    if (tick != NULL && atof(tick) > 0.0) s_tick = (float)atof(tick);
    char const* tas = getenv("HOST_MOVIE_TAS");
    r.tas           = tas != NULL && tas[0] == '1';
    char const* lifts = getenv("HOST_MOVIE_LIFTS");
    r.lifts           = lifts != NULL && lifts[0] == '1';
    char const* badge = getenv("HOST_MOVIE_BADGE");
    r.badge           = badge != NULL && badge[0] == '1';
    static story_info_t info[64];
    char const* const   story_path = getenv("HOST_MOVIE_STORY");
    int const           n_info     = story_path != NULL && story_path[0] ? read_story(story_path, info, 64) : 0;
    if (n_info > 0 && (!r.badge || n_info != argc - 2)) {
        fprintf(stderr, "HOST_MOVIE_STORY: wants HOST_MOVIE_BADGE, and a line for each of the %d demos\n", argc - 2);
        return 1;
    }
    r.lifts |= r.badge && n_info == 0;  // a desk story's rounds have no lifts (main.c)
    if (r.badge)
        movie_hud_init();
    else
        titles(&r, "C", OPEN_S);
    char const* desks    = getenv("HOST_MOVIE_DESKS");
    char const* progress = getenv("HOST_MOVIE_PROGRESS");
    for (int a = 2; a < argc; a++) {
        int const           i  = demo_find(argv[a]);
        story_info_t const* si = n_info > 0 ? &info[a - 2] : NULL;
        // How far into the story: the music builds with it, and may lift.
        float at = argc > 3 ? (float)(a - 2) / (float)(argc - 3) : 0.0f;
        if (progress != NULL && *progress) {
            at = strtof(progress, (char**)&progress);
            if (*progress == ',') progress++;
        }
        sound_music_progress(at);
        sound_music_tune(getenv("HOST_MOVIE_MUSIC"));
        // The desk before it, if any: its pictures, and its sound.
        int const desk = desks != NULL && *desks ? (int)strtol(desks, (char**)&desks, 10) : 0;
        if (desks != NULL && *desks == ',') desks++;
        if (desk > 0) film_desk(&r, desk);
        static game_t g;
        game_load(&g, demo_chamber(i));
        static char story[sizeof(g.lv.story)];
        snprintf(story, sizeof(story), "%s", g.lv.story);
        if (si != NULL) personalise(story, sizeof(story));
        fprintf(r.txt, "S\t%s\t%s\t%s\n", g.lv.name, g.lv.hint, story);
        r.round = false;
        r.said  = NULL;
        if (si != NULL && si->kind == 'O') {
            // The outro: its cards from the desk, its ending from pack.txt,
            // as the badge reads them.
            static desk_data_t d;
            static pack_t      pk;
            static char        files[PACK_CHAMBERS][CHAMBER_ID_N];
            char               err[160], path[300];
            int                nf = 0;
            snprintf(path, sizeof(path), "%s/pack.txt", si->pack);
            char* const text = slurp(path);
            if (text == NULL || !pack_parse(text, &pk, files, &nf) || !desk_load(&d, si->pack, err, sizeof(err))) {
                fprintf(stderr, "%s: %s\n", si->pack, text == NULL ? "no pack.txt" : err);
                return 1;
            }
            free(text);
            char ending[sizeof(pk.ending)];
            snprintf(ending, sizeof(ending), "%s", pk.ending);
            personalise(ending, sizeof(ending));
            film_outro(&r, demo_chamber(i), &d, ending);
            continue;
        }
        if (si != NULL) {
            // main.c's load_chamber and play_chamber for a round -- or, a
            // broken draft, start_playtest once it is repaired.
            r.round    = true;
            r.headline = si->headline;
            personalise(si->line, sizeof(si->line));  // as round_exit does
            r.line     = si->line;
            if (si->fresh || si->broken) {
                hud_story_start();
                sound_say(story[0] ? story : NULL);
            }
            char msg[HUD_MESSAGE_N];
            if (si->broken)
                snprintf(msg, sizeof(msg), "Reach the exit: repaired. Esc: the debugger");
            else if (si->badge)
                snprintf(msg, sizeof(msg), "%.60s", g.lv.name);  // a chore, not a round
            else if (si->drafts > 1)
                snprintf(msg, sizeof(msg), "Round %d: %.30s, draft %d", si->round, g.lv.name, si->drafts);
            else
                snprintf(msg, sizeof(msg), "Round %d: %.40s", si->round, g.lv.name);
            hud_message(msg);
        }
        lift_sites(&g.lv, &s_sites);
        lift_depart_reset(&s_depart);
        s_exit_have = false;
        if (r.badge && si == NULL) {
            // main.c's load_chamber and play_chamber, at the top of the
            // shaft: the story begins, said and typed, and the name is up.
            sound_say(story[0] ? story : NULL);
            hud_story_start();
            hud_message(g.lv.name);
        }
        if (r.lifts && (a > 2 || r.badge)) {
            // Down into it, from the last one's lift -- on the badge the
            // first too, as Continue arrives (main.c's arrive_by_lift).
            static game_t ride;
            static lift_t l;
            ride = g;
            lift_down(&l, &ride, &s_sites.start);
            film_ride(&r, &ride, &l, g.lv.name, 0.0f, -1.0f, false);
        }
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
        if (!r.badge) sound_say(story[0] ? story : NULL);
        demo_run(i, cap, TICK, &st, film_tick, &r, pace);
        r.t0 += r.last;
        if (r.round && s_exit_have) film_verdict(&r);
        if (r.lifts && s_exit_have) {
            // Up and out of it: in, the doors shut, into the shaft.
            static lift_t l;
            lift_site_t   site;
            if (lift_exit_for(&s_exit.lv, &s_sites, s_exit.pl.pos, &site)) {
                lift_enter(&l, &s_exit, &site, false);
                film_ride(&r, &s_exit, &l, r.done, r.exit_t, r.exit_t, a + 1 >= argc);
            }
        }
    }
    if (!r.badge) titles(&r, "E", CLOSE_S);

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
