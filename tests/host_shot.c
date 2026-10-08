// Host screenshots: the game's own render.c drawn by a small software
// rasterizer standing in for SynthEngine3D, so the portal passes can be
// looked at without a badge. `make shots` writes build/shots/*.ppm.
//
// What it mimics: the camera basis and pinhole projection, the near
// clip, scene_begin() emptying the depth buffer but not the pixels, and
// per-face lighting. Textures: by default a textured face draws its flat
// colour with panel seams from the u, v; with HOST_SHOT_TEXTURES naming a
// directory of the textures as PPM files (tools/make_movie.py makes one),
// it samples them, nearest texel, as the engine does. What it does not:
// the PPA, timing.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chamber.h"
#include "demo.h"
#include "level.h"
#include "player.h"
#include "portal.h"
#include "render.h"
#include "synthengine3d.h"

#define W DISPLAY_LOG_W
#define H DISPLAY_LOG_H

static uint32_t s_px[W * H];
static uint8_t  s_owner[W * H];  // which pass of the frame last painted each pixel
static int      s_pass;
static float    s_depth[W * H];  // 1/z, 0 = empty -- or the badge's 16-bit depth, as a float (below)

// The badge keeps depth as a whole number, 1/z * 64000 * near clip, in 16
// bits, and a pixel is drawn only if it is strictly nearer: two surfaces
// closer than a step apart (z^2 / 3200 m: 8 mm at 5 m, 3 cm at 10 m) go to
// whichever was drawn first. Drawn so here too, unless
// HOST_SHOT_FLOAT_DEPTH is set, so the PC shows what the badge shows.
#define SCENE_DEPTH_SCALE (64000.0f * RENDER_NEAR_CLIP_Z)
static int   s_float_depth = -1;
static float depth_of(float w) {
    if (s_float_depth < 0) s_float_depth = getenv("HOST_SHOT_FLOAT_DEPTH") != NULL;
    if (s_float_depth) return w;
    float d = floorf(w * SCENE_DEPTH_SCALE);
    if (d < 0.0f) d = 0.0f;
    if (d > 65535.0f) d = 65535.0f;
    return d;
}
// And in the badge's order: the engine draws all flat triangles before all
// textured ones, and with depth_order on (glass or a fizzler in the room)
// sorts each list on its 16-bit key -- which decides who wins a depth tie.
// HOST_SHOT_SUBMIT_ORDER draws in submission order instead.
// HOST_SHOT_QUARTER renders at half width and height, sampling (2i, 2j),
// as the badge's quarter-resolution setting.
static int  s_quarter = -1, s_border = -1;
static bool s_depth_order;
static int  env_flag(int* f, char const* n) {
    if (*f < 0) *f = getenv(n) != NULL;
    return *f;
}
static void       flush_deferred(void);
static basis_t    s_basis;
static vec3_t     s_eye;
static bool       s_light_on;
static se_light_t s_light;

// --- The engine API render.c uses ---------------------------------------

void render_set_camera_6dof(float x, float y, float z, float yaw, float pitch, float roll) {
    s_eye   = v3(x, y, z);
    s_basis = basis_from_angles(yaw, pitch, roll);
}

void scene_begin(pax_buf_t* fb) {
    (void)fb;
    s_pass++;
    memset(s_depth, 0, sizeof(s_depth));
    flush_deferred();
}

void scene_render(se_render_mode_t mode) {
    (void)mode;
    flush_deferred();
}

void scene_set_options(se_scene_options_t const* opts) {
    s_depth_order = opts->depth_order;
}

void se_light_set(se_light_t const* light) {
    s_light_on = light != NULL;
    if (light) s_light = *light;
}

// The texture as a PPM from HOST_SHOT_TEXTURES, if that is set and has it:
// its texels, RGB565 as the engine keeps them.
static bool load_ppm(char const* path, se_texture_t* t) {
    char const* dir = getenv("HOST_SHOT_TEXTURES");
    if (dir == NULL || dir[0] == '\0') return false;
    char const* base = strrchr(path, '/');
    base             = base != NULL ? base + 1 : path;
    char ppm[256];
    snprintf(ppm, sizeof(ppm), "%s/%.*s.ppm", dir, (int)(strcspn(base, ".")), base);
    FILE* f = fopen(ppm, "rb");
    if (f == NULL) return false;
    int w = 0, h = 0, max = 0;
    if (fscanf(f, "P6 %d %d %d", &w, &h, &max) != 3 || w <= 0 || h <= 0 || (w & (w - 1)) || (h & (h - 1)) ||
        max != 255) {
        fclose(f);
        return false;
    }
    fgetc(f);
    t->texels = calloc((size_t)(w * h), sizeof(uint16_t));
    t->w      = w;
    t->h      = h;
    for (int i = 0; i < w * h; i++) {
        uint8_t rgb[3] = {0};
        if (fread(rgb, 1, 3, f) != 3) break;
        t->texels[i] = (uint16_t)((rgb[0] >> 3) << 11 | (rgb[1] >> 2) << 5 | rgb[2] >> 3);
    }
    fclose(f);
    while ((1 << t->w_log2) < w) t->w_log2++;
    return true;
}

se_texture_t* se_texture_load(char const* path, uint32_t flags) {
    (void)flags;
    se_texture_t* t = calloc(1, sizeof(*t));
    if (!load_ppm(path, t)) {
        t->texels = calloc(1, sizeof(uint16_t));
        t->w = t->h = 1;
    }
    t->mean_argb = strstr(path, "white")   ? 0xFFD8D8D0u
                   : strstr(path, "metal") ? 0xFF50545Au
                   : strstr(path, "goo")   ? 0xFF6A5A18u
                   : strstr(path, "cube")  ? 0xFF969AA0u
                   : strstr(path, "glass") ? 0xFF9ED8F0u
                   : strstr(path, "fizz")  ? 0xFF60B0FFu
                   : strstr(path, "jump")  ? 0xFFE08020u
                   : strstr(path, "field") ? 0xFFD03A26u
                                           : 0xFF30D060u;
    return t;
}

typedef struct {
    vec3_t c;  // camera space
    float  u, v;
} rv_t;

static uint32_t shade(uint32_t argb, float k) {
    uint32_t const r = (uint32_t)fminf(255.0f, (float)((argb >> 16) & 255) * k);
    uint32_t const g = (uint32_t)fminf(255.0f, (float)((argb >> 8) & 255) * k);
    uint32_t const b = (uint32_t)fminf(255.0f, (float)(argb & 255) * k);
    return 0xFF000000u | r << 16 | g << 8 | b;
}

static uint32_t s_blend;  // this triangle mixes 50/50 with what is there (SE_TRI_BLEND)

// What a textured triangle samples, and how lit it is: set by submit().
static se_texture_t const* s_tex;
static float               s_lit;

static void raster(rv_t const* a, rv_t const* b, rv_t const* c, uint32_t col, bool seams) {
    float       sx[3], sy[3], iz[3], uz[3], vz[3];
    rv_t const* v[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        iz[i] = 1.0f / v[i]->c.z;
        sx[i] = RENDER_HALF_W + RENDER_FOCAL_LEN * v[i]->c.x * iz[i];
        sy[i] = RENDER_HORIZON_Y - RENDER_FOCAL_LEN * v[i]->c.y * iz[i];
        if (env_flag(&s_quarter, "HOST_SHOT_QUARTER")) sx[i] *= 0.5f, sy[i] *= 0.5f;
        uz[i] = v[i]->u * iz[i];
        vz[i] = v[i]->v * iz[i];
    }
    float const area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (fabsf(area) < 1e-6f) return;
    int x0 = (int)floorf(fminf(sx[0], fminf(sx[1], sx[2])));
    int x1 = (int)ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2])));
    int y0 = (int)floorf(fminf(sy[0], fminf(sy[1], sy[2])));
    int y1 = (int)ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2])));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    int const qw = s_quarter ? W / 2 : W, qh = s_quarter ? H / 2 : H;
    if (x1 > qw - 1) x1 = qw - 1;
    if (y1 > qh - 1) y1 = qh - 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float const px = (float)x + (s_quarter ? 0.0f : 0.5f), py = (float)y + (s_quarter ? 0.0f : 0.5f);
            float const w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
            float const w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
            float const w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float const z  = w0 * iz[0] + w1 * iz[1] + w2 * iz[2];
            float const dz = depth_of(z);
            if (dz <= s_depth[y * W + x]) continue;
            uint32_t out = col;
            if (s_tex != NULL && s_tex->w > 1) {
                float const    u  = (w0 * uz[0] + w1 * uz[1] + w2 * uz[2]) / z;
                float const    t  = (w0 * vz[0] + w1 * vz[1] + w2 * vz[2]) / z;
                int const      tx = (int)floorf(u * (float)s_tex->w) & (s_tex->w - 1);
                int const      ty = (int)floorf(t * (float)s_tex->h) & (s_tex->h - 1);
                uint16_t const p  = s_tex->texels[ty * s_tex->w + tx];
                if (p == SE_TEXEL_CUTOUT) continue;
                uint32_t const r = (p >> 11) * 255 / 31, g = ((p >> 5) & 63) * 255 / 63, bl = (p & 31) * 255 / 31;
                out   = shade(0xFF000000u | r << 16 | g << 8 | bl, s_lit);
                seams = false;
            }
            s_depth[y * W + x] = dz;
            if (seams) {
                float const u  = (w0 * uz[0] + w1 * uz[1] + w2 * uz[2]) / z;
                float const t  = (w0 * vz[0] + w1 * vz[1] + w2 * vz[2]) / z;
                float const fu = u - floorf(u), fv = t - floorf(t);
                if (fu < 0.03f || fu > 0.97f || fv < 0.03f || fv > 0.97f) out = shade(col, 0.6f);
            }
            if (s_blend) {
                uint32_t const o = s_px[y * W + x];
                out              = 0xFF000000u | ((((o >> 16) & 255) + ((out >> 16) & 255)) / 2) << 16 |
                                   ((((o >> 8) & 255) + ((out >> 8) & 255)) / 2) << 8 | (((o & 255) + (out & 255)) / 2);
            }
            s_px[y * W + x]    = out;
            s_owner[y * W + x] = (uint8_t)s_pass;
        }
    }
}

typedef struct {
    rv_t                v[3];
    uint32_t            col;
    se_texture_t const* tex;
    float               lit;
    uint32_t            blend;
    bool                textured;
    int                 idx;
} dtri_t;
static dtri_t   s_dl[65536];
static int      s_dn;
static uint32_t dkey(dtri_t const* t) {
    float ws = 0.0f;
    for (int i = 0; i < 3; i++) ws += 1.0f / fmaxf(t->v[i].c.z, RENDER_NEAR_CLIP_Z);
    uint32_t bits;
    memcpy(&bits, &ws, sizeof(bits));
    uint32_t const d = (bits >> 16) & 0x7FFFu;
    return t->blend ? (0x8000u | d) : (0x7FFFu - d);
}
static int dcmp(void const* pa, void const* pb) {
    dtri_t const * a = pa, *b = pb;
    uint32_t const ka = dkey(a), kb = dkey(b);
    if (ka != kb) return ka < kb ? -1 : 1;
    return a->idx - b->idx;  // stable, as the radix sort
}
static void flush_deferred(void) {
    if (s_dn == 0) return;
    static dtri_t tmp[65536];
    int           n = 0;
    for (int pass = 0; pass < 2; pass++) {
        int const from = n;
        for (int i = 0; i < s_dn; i++)
            if (s_dl[i].textured == (pass == 1)) tmp[n++] = s_dl[i];
        if (s_depth_order) qsort(tmp + from, (size_t)(n - from), sizeof(dtri_t), dcmp);
    }
    for (int i = 0; i < n; i++) {
        s_tex   = tmp[i].tex;
        s_lit   = tmp[i].lit;
        s_blend = tmp[i].blend;
        raster(&tmp[i].v[0], &tmp[i].v[1], &tmp[i].v[2], tmp[i].col, tmp[i].textured);
    }
    s_dn = 0;
}

static void submit(vec3_t const w[3], float const u[3], float const v[3], uint32_t argb, uint32_t flags, bool seams) {
    // Per-face light, after se_light: a floor of fill plus a directional share.
    uint32_t col = argb;
    s_lit        = 1.0f;
    if (s_light_on && !(flags & SE_TRI_EMISSIVE)) {
        vec3_t const n = v3_norm(v3_cross(v3_sub(w[1], w[0]), v3_sub(w[2], w[0])));
        vec3_t const c = v3_scale(v3_add(v3_add(w[0], w[1]), w[2]), 1.0f / 3.0f);
        vec3_t const l = v3_norm(v3_sub(v3(s_light.x, s_light.y, s_light.z), c));
        float const  d = fabsf(v3_dot(n, l));
        s_lit          = (1.0f - s_light.brightness) + s_light.brightness * d;
        col            = shade(argb, s_lit);
    }
    // The game's own light (SE_TRI_LIGHT) multiplied in, and its glow
    // (SE_TRI_GLOW) added, as the engine does: shade() saturates.
    if (flags & (SE_TRI_LIGHT_MASK | SE_TRI_GLOW_MASK)) {
        s_lit = s_lit * (float)se_tri_light_level(flags) / 32.0f + (float)se_tri_glow_level(flags) / 32.0f;
        col   = shade(argb, s_lit);
    }
    // To camera space, then clip at the near plane.
    rv_t in[3], buf[2][8];
    for (int i = 0; i < 3; i++) {
        vec3_t const d = v3_sub(w[i], s_eye);
        in[i] = (rv_t){v3(v3_dot(d, s_basis.right), v3_dot(d, s_basis.up), v3_dot(d, s_basis.fwd)), u[i], v[i]};
    }
    int n = 0;
    for (int i = 0; i < 3; i++) {
        rv_t const* a  = &in[i];
        rv_t const* b  = &in[(i + 1) % 3];
        float const da = a->c.z - RENDER_NEAR_CLIP_Z, db = b->c.z - RENDER_NEAR_CLIP_Z;
        if (da >= 0) buf[0][n++] = *a;
        if ((da >= 0) != (db >= 0)) {
            float const t = da / (da - db);
            buf[0][n++]   = (rv_t){v3_lerp(a->c, b->c, t), a->u + (b->u - a->u) * t, a->v + (b->v - a->v) * t};
        }
    }
    s_blend = flags & SE_TRI_BLEND;
    if (!env_flag(&s_border, "HOST_SHOT_SUBMIT_ORDER")) {
        for (int i = 1; i + 1 < n && s_dn < (int)(sizeof(s_dl) / sizeof(s_dl[0])); i++)
            s_dl[s_dn++] = (dtri_t){{buf[0][0], buf[0][i], buf[0][i + 1]}, col, s_tex, s_lit, s_blend, seams, s_dn};
        return;
    }
    for (int i = 1; i + 1 < n; i++) raster(&buf[0][0], &buf[0][i], &buf[0][i + 1], col, seams);
}

void scene_tri(float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, uint32_t argb,
               uint32_t flags) {
    vec3_t const w[3] = {v3(x0, y0, z0), v3(x1, y1, z1), v3(x2, y2, z2)};
    float const  u[3] = {0}, v[3] = {0};
    s_tex = NULL;
    submit(w, u, v, argb, flags, false);
}

void scene_textured_tri(se_tex_vertex_t const tv[3], se_texture_t const* tex, uint32_t flags) {
    vec3_t const w[3] = {v3(tv[0].x, tv[0].y, tv[0].z), v3(tv[1].x, tv[1].y, tv[1].z), v3(tv[2].x, tv[2].y, tv[2].z)};
    float const  u[3] = {tv[0].u, tv[1].u, tv[2].u}, v[3] = {tv[0].v, tv[1].v, tv[2].v};
    s_tex = tex;
    submit(w, u, v, tex->mean_argb, flags, true);
}

// --- Shots ----------------------------------------------------------------

static void save(char const* name) {
    char        path[256];
    char const* build = getenv("BUILD");  // the Makefile's $(BUILD)
    snprintf(path, sizeof(path), "%s/shots/%s.ppm", build != NULL && build[0] ? build : "build", name);
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        int const     j      = s_quarter > 0 ? (i / W / 2) * W + (i % W) / 2 : i;
        uint8_t const rgb[3] = {(uint8_t)(s_px[j] >> 16), (uint8_t)(s_px[j] >> 8), (uint8_t)s_px[j]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    int passes, tris;
    render_stats(&passes, &tris);
    printf("%-22s %d passes, %4d triangles\n", name, passes, tris);
}

static void shot(char const* name, level_t const* lv, portal_t const portals[2], vec3_t feet, float yaw, float pitch) {
    for (int i = 0; i < W * H; i++) s_px[i] = 0xFFFF00FFu;  // magenta: never drawn
    static game_t g;
    memset(&g, 0, sizeof(g));
    g.lv         = *lv;
    g.pl         = (player_t){.pos = feet, .yaw = yaw, .pitch = pitch};
    g.portals[0] = portals[0];
    g.portals[1] = portals[1];
    g.held       = -1;
    render_set_level(lv, portals);
    render_frame(NULL, &g);
    save(name);
}

static float yaw_to(vec3_t from, vec3_t to) {
    return atan2f(to.x - from.x, to.z - from.z);
}

// `host_shot demo <name> <t> ...`: frames of a scripted demo, as the
// device test's shots would show them.
static int demo_shots(int argc, char** argv) {
    int const i = demo_find(argv[2]);
    if (i < 0) {
        fprintf(stderr, "no demo %s\n", argv[2]);
        return 1;
    }
    static demo_state_t st;
    for (int k = 3; k < argc; k++) {
        float const t = (float)atof(argv[k]);
        demo_eval(i, t, &st);
        char name[64];
        snprintf(name, sizeof(name), "%s_%05d", argv[2], (int)(t * 1000.0f + 0.5f));
        for (int p = 0; p < W * H; p++) s_px[p] = 0xFFFF00FFu;
        render_set_level(&st.g.lv, st.g.portals);
        render_set_time(t);
        render_frame(NULL, &st.g);
        save(name);
        printf("   eye %.2f %.2f %.2f yaw %.2f pitch %.2f\n", player_eye(&st.g.pl).x, player_eye(&st.g.pl).y,
               player_eye(&st.g.pl).z, st.g.pl.yaw, st.g.pl.pitch);
    }
    return 0;
}

// `host_shot fuzz <n>`: random wall-portal pairs in chamber 1, walked
// into at random angles. Per crossing: pixels no pass drew (magenta),
// and how far the first frame after the teleport is from the last one
// before it -- what you saw through the portal should be what you get.
static uint32_t s_before[W * H];

static float frame_diff(void) {
    double sum = 0;
    for (int i = 0; i < W * H; i++) {
        uint32_t const a = s_before[i], b = s_px[i];
        sum += abs((int)((a >> 16) & 255) - (int)((b >> 16) & 255)) +
               abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255)) + abs((int)(a & 255) - (int)(b & 255));
    }
    return (float)(sum / (W * H * 3.0));
}

static int magenta(void) {
    int n = 0;
    for (int i = 0; i < W * H; i++) n += s_px[i] == 0xFFFF00FFu;
    return n;
}

static void draw(level_t const* lv, player_t const* pl, portal_t const pt[2]) {
    static game_t g;
    memset(&g, 0, sizeof(g));
    g.lv         = *lv;
    g.pl         = *pl;
    g.portals[0] = pt[0];
    g.portals[1] = pt[1];
    g.held       = -1;
    for (int i = 0; i < W * H; i++) s_px[i] = 0xFFFF00FFu;
    render_set_level(lv, pt);
    render_frame(NULL, &g);
}

static int fuzz(int n) {
    srand(7);
    level_t lv;
    int     worst_i = -1, bad = 0;
    float   worst = 0;
    for (int k = 0; k < n; k++) {
        level_load(&lv, 0);
        portal_t pt[2] = {0};
        for (int w = 0; w < 2; w++) {
            for (int tries = 0; tries < 100 && !pt[w].open; tries++) {
                int const face    = rand() % 4;  // the four walls
                int const dirs[4] = {DIR_PX, DIR_NX, DIR_PZ, DIR_NZ};
                int       x = 1 + rand() % 8, z = 1 + rand() % 14;
                int const f = dirs[face];
                if (f == DIR_PX) x = 0;
                if (f == DIR_NX) x = 9;
                if (f == DIR_PZ) z = 0;
                if (f == DIR_NZ) z = 15;
                portal_place_at(&lv, x, 1, z, f, v3(0, 1, 0), &pt[w ^ 1], &pt[w]);
            }
        }
        if (!pt[0].open || !pt[1].open) continue;
        // Walk at blue from 2 m out, at an angle.
        float const  ang       = ((float)rand() / RAND_MAX - 0.5f) * 1.0f;
        vec3_t const into      = v3_scale(pt[0].n, -1.0f);
        player_t     pl        = {0};
        pl.pos                 = v3_mad(v3(pt[0].center.x, 1.0f, pt[0].center.z), pt[0].n, 2.0f);
        pl.yaw                 = atan2f(into.x, into.z) + ang;
        pl.pitch               = ((float)rand() / RAND_MAX - 0.5f) * 0.6f;
        float const keep_pitch = pl.pitch;
        for (int i = 0; i < 200; i++) {
            player_t const       prev = pl;
            player_input_t const in   = {.fwd = 1.0f};
            int const            ev   = player_update(&pl, &lv, pt, &in, 1.0f / 50.0f);
            pl.pitch                  = ev & PL_EV_TELEPORT ? pl.pitch : keep_pitch;
            if (ev & PL_EV_TELEPORT) {
                draw(&lv, &prev, pt);
                memcpy(s_before, s_px, sizeof(s_px));
                int const m0 = magenta();
                draw(&lv, &pl, pt);
                int const   m1 = magenta();
                float const d  = frame_diff();
                if (d > worst) {
                    worst   = d;
                    worst_i = k;
                }
                if (d > 25.0f || m0 > 50 || m1 > 50) {
                    bad++;
                    if (bad <= 6) {
                        printf(
                            "case %d: diff %.1f magenta %d/%d  blue face %d (%.1f,%.1f) orange face %d (%.1f,%.1f) ang "
                            "%.2f\n",
                            k, d, m0, m1, pt[0].face, pt[0].center.x, pt[0].center.z, pt[1].face, pt[1].center.x,
                            pt[1].center.z, ang);
                        char name[32];
                        snprintf(name, sizeof(name), "fuzz%d_after", k);
                        save(name);
                        memcpy(s_px, s_before, sizeof(s_px));
                        snprintf(name, sizeof(name), "fuzz%d_before", k);
                        save(name);
                    }
                }
                break;
            }
        }
    }
    printf("fuzz: %d crossings flagged; worst diff %.1f (case %d)\n", bad, worst, worst_i);
    return 0;
}

// --- Self-test: make check runs it -------------------------------------

// Whether pixel (x, y)'s ray meets portal `p` inside its opening.
static bool in_opening(portal_t const* p, player_t const* pl, int x, int y) {
    basis_t const b   = player_view(pl);
    vec3_t const  e   = player_eye(pl);
    vec3_t const  d   = v3_add(b.fwd, v3_add(v3_scale(b.right, ((float)x + 0.5f - RENDER_HALF_W) / RENDER_FOCAL_LEN),
                                             v3_scale(b.up, -((float)y + 0.5f - RENDER_HORIZON_Y) / RENDER_FOCAL_LEN)));
    float const   den = v3_dot(d, p->n);
    if (fabsf(den) < 1e-6f) return false;
    float const t = v3_dot(v3_sub(p->center, e), p->n) / den;
    if (t <= 0.0f) return false;
    vec3_t const l = portal_local(p, v3_mad(e, d, t));
    // Inside the rim: 0.9 of the oval, clear of the rim's own pixels.
    float const  u = l.x / (PORTAL_HALF_W * 0.9f), v = l.y / (PORTAL_HALF_H * 0.9f);
    return u * u + v * v < 1.0f;
}

// Draw `g`; count the pixels in portal `which`'s opening that the last
// pass -- the room -- painted, over the view through the portal, and
// the pixels nothing painted at all.
static void frame_check(char const* what, game_t* g, int which, int* fails) {
    for (int i = 0; i < W * H; i++) s_px[i] = 0xFFFF00FFu;
    memset(s_owner, 0, sizeof(s_owner));
    s_pass = 0;
    render_set_level(&g->lv, g->portals);
    render_frame(NULL, g);
    int over = 0, inside = 0, holes = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (s_px[y * W + x] == 0xFFFF00FFu) holes++;
            if (!in_opening(&g->portals[which], &g->pl, x, y)) continue;
            inside++;
            if (s_owner[y * W + x] == s_pass) over++;
        }
    bool const ok = inside > 0 && over == 0 && holes == 0;
    printf("%-34s %6d px in the opening, %5d painted over by the room, %d never painted: %s\n", what, inside, over,
           holes, ok ? "ok" : "FAIL");
    if (!ok) (*fails)++;
}

// After each step that moves a portal or paints: the kept mesh is the
// whole mesh, rectangle for rectangle.
static int  mesh_ok_n, mesh_bad_n;
static void mesh_tick(game_t const* g, int ev, float now, void* ctx) {
    (void)now;
    (void)ctx;
    if (!(ev & (GAME_EV_PORTAL | GAME_EV_PAINT)) && mesh_ok_n > 0) return;
    render_set_level(&g->lv, g->portals);
    static mquad_t whole[LV_MAX_QUADS];
    hole_t         holes[4];
    int            nh = 0;
    for (int i = 0; i < 2; i++)
        if (g->portals[i].open)
            for (int c = 0; c < 2; c++)
                holes[nh++] = (hole_t){g->portals[i].cell[c][0], g->portals[i].cell[c][1], g->portals[i].cell[c][2],
                                       g->portals[i].face};
    int const      n = level_mesh(&g->lv, holes, nh, whole, LV_MAX_QUADS);
    mquad_t const* kept;
    int const      k = render_quads(&kept);
    if (k != n || memcmp(kept, whole, (size_t)n * sizeof(mquad_t)) != 0) mesh_bad_n++;
    mesh_ok_n++;
}

static int selftest(void) {
    static game_t  g;
    static level_t thin;
    char           err[96];
    int            fails = 0;

    // Chamber 09: a floor portal on the start ledge, its far end by the goo
    // pit; below the floor is open space.
    game_load(&g, demo_chamber(demo_find("09-the-ferry")));
    portal_place_at(&g.lv, 2, 1, 3, DIR_PY, v3(1, 0, 0), NULL, &g.portals[0]);
    portal_place_at(&g.lv, 13, 1, 1, DIR_PY, v3(1, 0, 0), &g.portals[0], &g.portals[1]);
    g.pl.pos   = v3(1.4f, 2.0f, 3.5f);
    g.pl.yaw   = 1.5707963f;
    g.pl.pitch = 0.75f;
    frame_check("floor portal over open space", &g, 0, &fails);

    // A wall one cell thick, as the editor makes them, with a room behind.
    if (!chamber_parse(
            "size: 8 5 9\nlayer 0\n########\n#WWWWWW#\n#WWEEWW#\n#WWWWWW#\n#WWWWWW#\n#WWWWWW#\n"
            "#WWWWWW#\n#WWWWWW#\n########\n"
            "layer 1\n########\n#......#\n#......#\n#......#\n##WWW###\n#......#\n#......#\n#...S..#\n########\n"
            "layer 2\n########\n#......#\n#......#\n#......#\n##WWW###\n#......#\n#......#\n#......#\n########\n"
            "layer 3\n########\n#......#\n#......#\n#......#\n########\n#......#\n#......#\n#......#\n########\n",
            &thin, NULL, NULL, err, sizeof(err))) {
        printf("thin wall chamber: %s\n", err);
        return 1;
    }
    game_load_level(&g, &thin);
    portal_place_at(&g.lv, 3, 1, 4, DIR_NZ, v3(0, 1, 0), NULL, &g.portals[0]);
    portal_place_at(&g.lv, 1, 0, 1, DIR_PY, v3(0, 0, 1), &g.portals[0], &g.portals[1]);
    g.pl.pos   = v3(3.5f, 1.0f, 1.5f);
    g.pl.yaw   = 0.0f;
    g.pl.pitch = 0.05f;
    frame_check("portal in a wall one cell thick", &g, 0, &fails);

    // The eye half a millimetre in front of an opening, about to go through.
    g.pl.pos = v3(3.5f, 1.0f, 4.0f - 0.0005f);
    frame_check("eye half a millimetre from a portal", &g, 0, &fails);

    // The mesh, kept slice by slice, against a whole fresh one: through
    // every chamber's solution, at every portal shot and every new paint.
    int checked = 0, wrong = 0;
    for (int i = 0; i < demo_count(); i++) {
        if (!demo_has_solution(i)) continue;
        static demo_state_t st;
        mesh_ok_n = 0;
        demo_run(i, 40.0f, 1.0f / 30.0f, &st, mesh_tick, NULL, 0.0f);
        checked    += mesh_ok_n;
        wrong      += mesh_bad_n;
        mesh_bad_n  = 0;
    }
    // A restart: the same cells made anew, the paint gone. And more paint
    // than the log keeps, at once.
    {
        static game_t r;
        int const     gel = demo_chamber(demo_find("14-repulsion"));
        game_load(&r, gel);
        mesh_ok_n = 1;
        mesh_tick(&r, GAME_EV_PAINT, 0.0f, NULL);  // fresh
        for (int i = 0; i < LV_PAINT_LOG + 40; i++)
            level_set_paint(&r.lv, 1 + i % (r.lv.w - 2), 0, 1 + i / (r.lv.w - 2) % (r.lv.d - 2), GEL_ORANGE);
        mesh_tick(&r, GAME_EV_PAINT, 0.0f, NULL);  // past the log
        level_set_paint(&r.lv, 2, 0, 2, GEL_BLUE);
        mesh_tick(&r, GAME_EV_PAINT, 0.0f, NULL);  // from the log
        game_load(&r, gel);
        mesh_tick(&r, GAME_EV_PAINT, 0.0f, NULL);  // restarted
        checked += mesh_ok_n - 1;
        wrong   += mesh_bad_n;
    }
    printf("mesh kept by slices                %d changes checked against a whole mesh: %s\n", checked,
           wrong == 0 && checked > 0 ? "ok" : "WRONG");
    if (wrong || checked == 0) fails++;
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    render_init("textures");
    // More chambers, as the badge reads them from the SD card.
    char const* extra = getenv("PORTALS_CHAMBERS");
    if (extra != NULL) chamber_load_dir(extra);
    if (argc > 1 && strcmp(argv[1], "selftest") == 0) return selftest();
    if (argc > 2 && strcmp(argv[1], "fuzz") == 0) return fuzz(atoi(argv[2]));
    if (argc > 2 && strcmp(argv[1], "demo") == 0) return demo_shots(argc, argv);
    level_t  lv;
    portal_t p[2] = {0};

    // 1. Two portals on the left wall of the first chamber; looking at
    //    the near one shows the far end of the room, goo and the exit.
    level_load(&lv, 0);
    portal_place_at(&lv, 0, 1, 3, DIR_PX, v3(0, 1, 0), NULL, &p[0]);
    portal_place_at(&lv, 0, 1, 12, DIR_PX, v3(0, 1, 0), &p[0], &p[1]);
    shot("c1_overview", &lv, p, v3(6.5f, 1, 1.5f), 0.4f - 1.5707963f * 0.5f, 0.05f);
    shot("c1_into_blue", &lv, p, v3(4.0f, 1, 3.5f), -1.5707963f, 0.0f);
    shot("c1_close_blue", &lv, p, v3(1.4f, 1, 3.5f), -1.5707963f, 0.1f);
    // The eye a hair from the plane, the frame before it goes through.
    shot("c1_eye_at_plane", &lv, p, v3(1.01f, 1, 3.5f), -1.5707963f, 0.0f);
    shot("c1_eye_at_plane_skew", &lv, p, v3(1.01f, 1, 3.6f), -1.2f, 0.2f);
    portal_t one[2] = {p[0], {0}};
    shot("c1_one_portal", &lv, one, v3(4.0f, 1, 3.5f), -1.5707963f, 0.0f);

    // 2. Facing each other across the room: the views nest.
    portal_t f[2] = {0};
    portal_place_at(&lv, 0, 1, 12, DIR_PX, v3(0, 1, 0), NULL, &f[0]);
    portal_place_at(&lv, 9, 1, 12, DIR_NX, v3(0, 1, 0), &f[0], &f[1]);
    for (int d = 1; d <= RENDER_PORTAL_DEPTH_MAX; d++) {
        char name[32];
        render_set_portal_depth(d);
        snprintf(name, sizeof(name), "c1_facing_depth%d", d);
        shot(name, &lv, f, v3(6.0f, 1, 12.5f), -1.5707963f, 0.0f);
    }
    render_set_portal_depth(2);

    // 3. The ledge: blue low on the left wall shows the top of the ledge.
    level_load(&lv, 1);
    portal_place_at(&lv, 0, 1, 4, DIR_PX, v3(0, 1, 0), NULL, &p[0]);
    portal_place_at(&lv, 5, 6, 13, DIR_NZ, v3(0, 1, 0), &p[0], &p[1]);
    shot("c2_blue_to_ledge", &lv, p, v3(4.5f, 1, 4.5f), yaw_to(v3(4.5f, 0, 4.5f), p[0].center), 0.05f);

    // 4. The fling: from the balcony, the floor portal below.
    level_load(&lv, 2);
    portal_place_at(&lv, 6, 0, 5, DIR_PY, v3(0, 0, 1), NULL, &p[0]);
    portal_place_at(&lv, 0, 8, 5, DIR_PX, v3(0, 1, 0), &p[0], &p[1]);
    shot("c3_balcony_down", &lv, p, v3(6.5f, 11, 2.7f), 0.0f, 1.2f);
    shot("c3_balcony_west", &lv, p, v3(6.5f, 11, 2.7f), -1.3f, 0.4f);
    return 0;
}
