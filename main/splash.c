#include "splash.h"
#include <math.h>
#include <string.h>
#include "chamber.h"
#include "level.h"
#include "pax_fonts.h"
#include "portal.h"
#include "render.h"
#include "se_text.h"
#include "sound.h"
#include "synthengine3d.h"

#define SPLASH_S   3.6f  // all of it
#define FADE_IN_S  0.5f
#define FADE_OUT_S 0.6f
#define TITLE_AT_S 1.0f  // the title comes in

// The corridor: short and narrow, so the portals at its ends fill the
// view and the corridor beyond them -- the same one, again and again --
// stays big. White where the portals go, a pellet launcher in the west
// wall, metal everywhere else: the light is the portals' and the pellet's.
static char const s_corridor[] =
    "name: Splash\n"
    "size: 5 6 9\n"
    "facing: north\n"
    "layer 1\n"
    "##W##\n#...#\n#...#\n#...#\n#C..#\n#...#\n#...#\n#.S.#\n##W##\n"
    "layer 2\n"
    "##W##\n#...#\n#...#\n#...#\n#...#\n#...#\nP...#\n#...#\n##W##\n"
    "layer 3\n"
    "#####\n#...#\n#...#\n#...#\n#...#\n#...#\n#...#\n#...#\n#####\n"
    "layer 4\n"
    "#####\n#...#\n#...#\n#...#\n#...#\n#...#\n#...#\n#...#\n#####\n";

static float s_t = -1.0f;

static float ease(float x) {
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return x * x * (3.0f - 2.0f * x);
}

bool splash_start(game_t* g) {
    level_t* const lv = level_scratch();
    char           err[96];
    if (!chamber_parse(s_corridor, lv, NULL, NULL, err, sizeof(err))) return false;
    game_load_level(g, lv);
    // Blue on the far wall, orange on the near one, facing each other.
    portal_t blue, orange;
    if (!portal_place_at(&g->lv, 2, 1, 8, DIR_NZ, v3(0, 1, 0), NULL, &blue) ||
        !portal_place_at(&g->lv, 2, 1, 0, DIR_PZ, v3(0, 1, 0), &blue, &orange))
        return false;
    g->portals[0] = blue;
    g->portals[1] = orange;
    s_t           = 0.0f;
    // As deep as the renderer goes: the corridor, again and again.
    render_set_portal_depth(RENDER_PORTAL_DEPTH_MAX);
    sound_play(SND_SHOT_BLUE);
    return true;
}

bool splash_update(game_t* g, float dt) {
    if (s_t < 0.0f) return false;
    float const t = s_t += dt;
    if (t >= SPLASH_S) {
        s_t = -1.0f;
        return false;
    }
    // The camera: a slow glide down the corridor towards the blue, a
    // little sway, never quite through it.
    float const k = ease(t / SPLASH_S);
    g->pl.pos     = v3(2.5f + 0.2f * sinf(t * 1.3f), 1.0f, 1.6f + 4.2f * k);
    g->pl.yaw     = 0.06f * sinf(t * 0.9f);
    g->pl.pitch   = 0.04f * sinf(t * 1.1f + 0.5f);
    // The pellet: down the corridor ahead of us, into the blue at the far
    // wall and out of the orange at the near one, again and again.
    float z               = fmodf(1.2f + t * 6.0f, 7.0f) + 1.0f;  // between the two walls, z 1..8
    g->pellets[0].pos     = v3(2.5f + 0.45f * sinf(t * 3.1f), 2.0f + 0.3f * sinf(t * 2.3f), z);
    g->pellets[0].live    = true;
    g->pellets[0].done    = false;
    if (t > 0.3f && t - dt <= 0.3f) sound_play(SND_SHOT_ORANGE);
    if (t > 0.7f && t - dt <= 0.7f) sound_play(SND_PELLET);
    return true;
}

void splash_skip(void) {
    if (s_t >= 0.0f && s_t < SPLASH_S - FADE_OUT_S) s_t = SPLASH_S - FADE_OUT_S;
}

float splash_lit(void) {
    if (s_t < 0.0f) return 0.0f;
    float const in = s_t / FADE_IN_S, out = (SPLASH_S - s_t) / FADE_OUT_S;
    float       l  = in < out ? in : out;
    return l < 0 ? 0 : l > 1 ? 1 : l;
}

// `text` centred on `cy`, at `size`, thick: drawn a few times over, a
// pixel apart (the vector font's strokes are a pixel wide).
static void bold(pax_buf_t* fb, pax_col_t col, float size, float cy, char const* text, int thick) {
    pax_vec2f const s = rendertext_size(pax_font_sky_mono, size, text);
    float const     x = ((float)DISPLAY_LOG_W - s.x) * 0.5f, y = cy - s.y * 0.5f;
    for (int dy = 0; dy < thick; dy++)
        for (int dx = 0; dx < thick; dx++) rendertext_draw(fb, col, pax_font_sky_mono, size, x + dx, y + dy, text);
}

static pax_col_t dim(uint32_t argb, float k) {
    k                = k < 0 ? 0 : k > 1 ? 1 : k;
    uint32_t const r = (uint32_t)((float)((argb >> 16) & 255) * k);
    uint32_t const g = (uint32_t)((float)((argb >> 8) & 255) * k);
    uint32_t const b = (uint32_t)((float)(argb & 255) * k);
    return 0xFF000000u | r << 16 | g << 8 | b;
}

void splash_draw(pax_buf_t* fb) {
    if (s_t < TITLE_AT_S) return;
    // The title comes in from a little too big, settling, as it brightens;
    // a rule under it, blue on the left and orange on the right, drawn out
    // from the middle.
    float const k    = ease((s_t - TITLE_AT_S) / 0.7f);
    float const lit  = k * splash_lit();
    float const size = 72.0f + 24.0f * (1.0f - k);
    float const cy   = (float)DISPLAY_LOG_H * 0.42f;
    bold(fb, dim(0xFF000000u, 1), size, cy + 3, "PORTALS", 3);  // a shadow
    bold(fb, dim(0xFFFFFFFFu, lit), size, cy, "PORTALS", 3);
    float const half = 190.0f * ease((s_t - TITLE_AT_S - 0.3f) / 0.6f);
    float const ry   = cy + size * 0.55f;
    if (half > 1.0f) {
        pax_simple_rect(fb, dim(0xFF2C8CFFu, splash_lit()), (float)DISPLAY_LOG_W * 0.5f - half, ry, half, 4);
        pax_simple_rect(fb, dim(0xFFFF8A1Cu, splash_lit()), (float)DISPLAY_LOG_W * 0.5f, ry, half, 4);
    }
    float const sub = ease((s_t - TITLE_AT_S - 0.6f) / 0.5f) * splash_lit();
    if (sub > 0.0f) bold(fb, dim(0xFFC8CCD4u, sub), 18.0f, ry + 26.0f, "for Tanmatsu", 1);
}
