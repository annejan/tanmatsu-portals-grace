// Host screenshots: the game's own render.c drawn by a small software
// rasterizer standing in for SynthEngine3D, so the portal passes can be
// looked at without a badge. `make shots` writes build/shots/*.ppm.
//
// What it mimics: the camera basis and pinhole projection, the near
// clip, scene_begin() emptying the depth buffer but not the pixels, and
// per-face lighting. What it does not: textures (a textured face draws
// its flat colour with panel seams from the u, v), the PPA, half
// resolution, timing.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "level.h"
#include "player.h"
#include "portal.h"
#include "render.h"
#include "synthengine3d.h"

#define W DISPLAY_LOG_W
#define H DISPLAY_LOG_H

static uint32_t s_px[W * H];
static float    s_depth[W * H];  // 1/z, 0 = empty
static basis_t  s_basis;
static vec3_t   s_eye;
static bool     s_light_on;
static se_light_t s_light;

// --- The engine API render.c uses ---------------------------------------

void render_set_camera_6dof(float x, float y, float z, float yaw, float pitch, float roll) {
    s_eye   = v3(x, y, z);
    s_basis = basis_from_angles(yaw, pitch, roll);
}

void scene_begin(pax_buf_t* fb) {
    (void)fb;
    memset(s_depth, 0, sizeof(s_depth));
}

void scene_render(se_render_mode_t mode) {
    (void)mode;
}

void scene_set_options(se_scene_options_t const* opts) {
    (void)opts;
}

void se_light_set(se_light_t const* light) {
    s_light_on = light != NULL;
    if (light) s_light = *light;
}

se_texture_t* se_texture_load(char const* path, uint32_t flags) {
    (void)flags;
    se_texture_t* t = calloc(1, sizeof(*t));
    t->texels       = calloc(1, sizeof(uint16_t));
    t->w = t->h  = 1;
    t->mean_argb = strstr(path, "white")  ? 0xFFD8D8D0u
                   : strstr(path, "metal") ? 0xFF50545Au
                   : strstr(path, "goo")   ? 0xFF6A5A18u
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

static void raster(rv_t const* a, rv_t const* b, rv_t const* c, uint32_t col, bool seams) {
    float sx[3], sy[3], iz[3], uz[3], vz[3];
    rv_t const* v[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        iz[i] = 1.0f / v[i]->c.z;
        sx[i] = RENDER_HALF_W + RENDER_FOCAL_LEN * v[i]->c.x * iz[i];
        sy[i] = RENDER_HORIZON_Y - RENDER_FOCAL_LEN * v[i]->c.y * iz[i];
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
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float const px = (float)x + 0.5f, py = (float)y + 0.5f;
            float const w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
            float const w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
            float const w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float const z = w0 * iz[0] + w1 * iz[1] + w2 * iz[2];
            if (z <= s_depth[y * W + x]) continue;
            s_depth[y * W + x] = z;
            uint32_t out       = col;
            if (seams) {
                float const u = (w0 * uz[0] + w1 * uz[1] + w2 * uz[2]) / z;
                float const t = (w0 * vz[0] + w1 * vz[1] + w2 * vz[2]) / z;
                float const fu = u - floorf(u), fv = t - floorf(t);
                if (fu < 0.03f || fu > 0.97f || fv < 0.03f || fv > 0.97f) out = shade(col, 0.6f);
            }
            s_px[y * W + x] = out;
        }
    }
}

static void submit(vec3_t const w[3], float const u[3], float const v[3], uint32_t argb, uint32_t flags, bool seams) {
    // Per-face light, after se_light: a floor of fill plus a directional share.
    uint32_t col = argb;
    if (s_light_on && !(flags & SE_TRI_EMISSIVE)) {
        vec3_t const n  = v3_norm(v3_cross(v3_sub(w[1], w[0]), v3_sub(w[2], w[0])));
        vec3_t const c  = v3_scale(v3_add(v3_add(w[0], w[1]), w[2]), 1.0f / 3.0f);
        vec3_t const l  = v3_norm(v3_sub(v3(s_light.x, s_light.y, s_light.z), c));
        float const  d  = fabsf(v3_dot(n, l));
        col             = shade(argb, (1.0f - s_light.brightness) + s_light.brightness * d);
    }
    // To camera space, then clip at the near plane.
    rv_t in[3], buf[2][8];
    for (int i = 0; i < 3; i++) {
        vec3_t const d = v3_sub(w[i], s_eye);
        in[i]          = (rv_t){v3(v3_dot(d, s_basis.right), v3_dot(d, s_basis.up), v3_dot(d, s_basis.fwd)), u[i], v[i]};
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
    for (int i = 1; i + 1 < n; i++) raster(&buf[0][0], &buf[0][i], &buf[0][i + 1], col, seams);
}

void scene_tri(float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, uint32_t argb,
               uint32_t flags) {
    vec3_t const w[3] = {v3(x0, y0, z0), v3(x1, y1, z1), v3(x2, y2, z2)};
    float const  u[3] = {0}, v[3] = {0};
    submit(w, u, v, argb, flags, false);
}

void scene_textured_tri(se_tex_vertex_t const tv[3], se_texture_t const* tex, uint32_t flags) {
    vec3_t const w[3] = {v3(tv[0].x, tv[0].y, tv[0].z), v3(tv[1].x, tv[1].y, tv[1].z), v3(tv[2].x, tv[2].y, tv[2].z)};
    float const  u[3] = {tv[0].u, tv[1].u, tv[2].u}, v[3] = {tv[0].v, tv[1].v, tv[2].v};
    submit(w, u, v, tex->mean_argb, flags, true);
}

// --- Shots ----------------------------------------------------------------

static void save(char const* name) {
    char path[128];
    snprintf(path, sizeof(path), "build/shots/%s.ppm", name);
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint8_t const rgb[3] = {(uint8_t)(s_px[i] >> 16), (uint8_t)(s_px[i] >> 8), (uint8_t)s_px[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    int passes, tris;
    render_stats(&passes, &tris);
    printf("%-22s %d passes, %4d triangles\n", name, passes, tris);
}

static void shot(char const* name, level_t const* lv, portal_t const portals[2], vec3_t feet, float yaw, float pitch) {
    for (int i = 0; i < W * H; i++) s_px[i] = 0xFFFF00FFu;  // magenta: never drawn
    player_t pl = {.pos = feet, .yaw = yaw, .pitch = pitch};
    render_set_level(lv, portals);
    render_frame(NULL, lv, &pl, portals);
    save(name);
}

static float yaw_to(vec3_t from, vec3_t to) {
    return atan2f(to.x - from.x, to.z - from.z);
}

int main(void) {
    render_init("textures");
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
