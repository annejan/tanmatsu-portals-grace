#include "render.h"
#include <stdio.h>
#include "esp_log.h"
#include "synthengine3d.h"

static char const TAG[] = "render";

typedef struct {
    vec3_t  pos;
    basis_t b;
} cam_t;

typedef struct {
    char const*   file;
    uint32_t      argb;  // without a texture, and its mean colour
    uint32_t      flags;
    se_texture_t* tex;
} material_info_t;

static material_info_t s_mat[MAT_COUNT] = {
    [MAT_WHITE]    = {"white.png", 0xFFD8D8D0u, 0, NULL},
    [MAT_METAL]    = {"metal.png", 0xFF44484Cu, 0, NULL},
    [MAT_GOO]      = {"goo.png", 0xFF5A4A18u, SE_TRI_EMISSIVE, NULL},
    [MAT_EXIT]     = {"exit.png", 0xFF30D060u, SE_TRI_EMISSIVE, NULL},
    [MAT_GLASS]    = {"glass.png", 0xFF9ED8F0u, SE_TRI_BLEND, NULL},
    [MAT_FIZZ]     = {"fizz.png", 0xFF60B0FFu, SE_TRI_BLEND | SE_TRI_EMISSIVE, NULL},
    [MAT_JUMP]     = {"jump.png", 0xFFE08020u, 0, NULL},
    // Flat colours: the pedestal a light grey block, the dropper dark.
    [MAT_PEDESTAL] = {NULL, 0xFF7A7E86u, 0, NULL},
    [MAT_DROPPER]  = {NULL, 0xFF34363Bu, 0, NULL},
    [MAT_CUBEBASE] = {NULL, 0xFF3E4A60u, 0, NULL},
};

static material_info_t s_cube = {"cube.png", 0xFF969AA0u, 0, NULL};

static uint32_t const s_rim[2]  = {0xFF2C8CFFu, 0xFFFF8A1Cu};
static uint32_t const s_shut[2] = {0xFF0C2850u, 0xFF502808u};  // one portal, nothing through it
static uint32_t const s_deep[2] = {0xFF184070u, 0xFF704018u};  // past the deepest view drawn

static mquad_t s_quads[LV_MAX_QUADS];
static int     s_nquads;
// What is seen through: glass faces and fizzler sheets. Drawn after
// everything solid, since a half-transparent triangle mixes with what is
// already there.
static mquad_t s_clear[LV_MAX_CLEAR];  // level_clear_faces() counts these the same way
static int     s_nclear;
static int     s_depth = 2;
static int     s_stat_passes, s_stat_tris;
static vec3_t  s_light;

void render_init(char const* texture_dir) {
    char path[192];
    for (int m = 0; m < MAT_COUNT; m++) {
        if (s_mat[m].file == NULL) continue;
        snprintf(path, sizeof(path), "%s/%s", texture_dir, s_mat[m].file);
        s_mat[m].tex = se_texture_load(path, SE_TEXTURE_INTERNAL);
        if (s_mat[m].tex == NULL) ESP_LOGW(TAG, "no texture %s: flat colour instead", path);
    }
    snprintf(path, sizeof(path), "%s/%s", texture_dir, s_cube.file);
    s_cube.tex = se_texture_load(path, SE_TEXTURE_INTERNAL);
    scene_set_options(&(se_scene_options_t){.frustum_cull = true, .depth_order = false});
}

static void add_clear(vec3_t o, vec3_t du, vec3_t dv, vec3_t n, uint8_t m) {
    if (s_nclear < LV_MAX_CLEAR) s_clear[s_nclear++] = (mquad_t){o, du, dv, n, 1.0f, 1.0f, m};
}

static void build_clear(level_t const* lv) {
    s_nclear = 0;
    for (int y = 0; y < lv->h; y++)
        for (int z = 0; z < lv->d; z++)
            for (int x = 0; x < lv->w; x++) {
                uint8_t const m = level_get(lv, x, y, z);
                if (m == MAT_GLASS) {
                    // Each face of a glass cell that looks onto open space.
                    for (int face = 0; face < 6; face++) {
                        int dx, dy, dz;
                        dir_step(face, &dx, &dy, &dz);
                        uint8_t const nb = level_get(lv, x + dx, y + dy, z + dz);
                        if (nb != MAT_AIR && nb != MAT_DOOR && nb != MAT_FIZZ) continue;
                        int const a = face / 2, ua = (a + 1) % 3, va = (a + 2) % 3;
                        float     o[3] = {(float)x, (float)y, (float)z}, du[3] = {0}, dv[3] = {0};
                        if (face % 2 == 0) o[a] += 1.0f;
                        du[ua] = 1.0f;
                        dv[va] = 1.0f;
                        add_clear(v3(o[0], o[1], o[2]), v3(du[0], du[1], du[2]), v3(dv[0], dv[1], dv[2]), dir_vec(face),
                                  m);
                    }
                } else if (m == MAT_FIZZ) {
                    // A sheet through the middle of the cell, across the
                    // way the fizzler runs, seen from both sides.
                    bool const along_x =
                        level_get(lv, x - 1, y, z) == MAT_FIZZ || level_get(lv, x + 1, y, z) == MAT_FIZZ ||
                        !(level_get(lv, x, y, z - 1) == MAT_FIZZ || level_get(lv, x, y, z + 1) == MAT_FIZZ);
                    if (along_x) {
                        vec3_t const o = v3((float)x, (float)y, (float)z + 0.5f);
                        add_clear(o, v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, -1), m);
                        add_clear(v3_add(o, v3(1, 0, 0)), v3(-1, 0, 0), v3(0, 1, 0), v3(0, 0, 1), m);
                    } else {
                        vec3_t const o = v3((float)x + 0.5f, (float)y, (float)z);
                        add_clear(o, v3(0, 1, 0), v3(0, 0, 1), v3(-1, 0, 0), m);
                        add_clear(v3_add(o, v3(0, 0, 1)), v3(0, 1, 0), v3(0, 0, -1), v3(1, 0, 0), m);
                    }
                }
            }
}

void render_set_level(level_t const* lv, portal_t const portals[2]) {
    hole_t holes[4];
    int    nh = 0;
    for (int i = 0; i < 2; i++) {
        if (!portals[i].open) continue;
        for (int c = 0; c < 2; c++)
            holes[nh++] =
                (hole_t){portals[i].cell[c][0], portals[i].cell[c][1], portals[i].cell[c][2], portals[i].face};
    }
    s_nquads = level_mesh(lv, holes, nh, s_quads, LV_MAX_QUADS);
    if (s_nquads > LV_MAX_QUADS) s_nquads = LV_MAX_QUADS;  // the parser refuses such chambers
    build_clear(lv);
    // Far off, so it is a direction: the engine lights each triangle on
    // its own, and a near light shades the two halves of a big merged
    // quad differently, leaving a seam along the diagonal.
    vec3_t const centre = v3((float)lv->w * 0.5f, (float)lv->h * 0.5f, (float)lv->d * 0.5f);
    s_light             = v3_mad(centre, v3_norm(v3(0.35f, 1.0f, -0.55f)), 5000.0f);
}

void render_set_portal_depth(int depth) {
    s_depth = depth < 1 ? 1 : depth > RENDER_PORTAL_DEPTH_MAX ? RENDER_PORTAL_DEPTH_MAX : depth;
}

int render_portal_depth(void) {
    return s_depth;
}

void render_stats(int* passes, int* tris) {
    if (passes) *passes = s_stat_passes;
    if (tris) *tris = s_stat_tris;
}

// --- Submitting ----------------------------------------------------------

static void emit_poly(cvert_t const* v, int n, material_info_t const* m, uint32_t argb, uint32_t flags) {
    for (int i = 1; i + 1 < n; i++) {
        if (m != NULL && m->tex != NULL) {
            se_tex_vertex_t const t[3] = {
                {v[0].p.x, v[0].p.y, v[0].p.z, v[0].u, v[0].v},
                {v[i].p.x, v[i].p.y, v[i].p.z, v[i].u, v[i].v},
                {v[i + 1].p.x, v[i + 1].p.y, v[i + 1].p.z, v[i + 1].u, v[i + 1].v},
            };
            scene_textured_tri(t, m->tex, flags);
        } else {
            scene_tri(v[0].p.x, v[0].p.y, v[0].p.z, v[i].p.x, v[i].p.y, v[i].p.z, v[i + 1].p.x, v[i + 1].p.y,
                      v[i + 1].p.z, argb, flags);
        }
        s_stat_tris++;
    }
}

// What this pass must not draw: for each portal whose opening it leaves
// empty, for the view through it drawn earlier to show, the room behind
// that portal as the eye sees it through the opening (portal_behind).
static clipset_t s_cut[2];
static int       s_ncut;

typedef struct {
    material_info_t const* m;
    uint32_t               argb, flags;
} paint_t;

static void emit_cut1(cvert_t const* v, int n, void* ctx) {
    paint_t const* p = ctx;
    emit_poly(v, n, p->m, p->argb, p->flags);
}

static void emit_cut0(cvert_t const* v, int n, void* ctx) {
    if (s_ncut > 1) {
        clip_subtract(&s_cut[1], v, n, 1, emit_cut1, ctx);
    } else {
        emit_cut1(v, n, ctx);
    }
}

// emit_poly, less the regions in s_cut.
static void emit(cvert_t const* v, int n, material_info_t const* m, uint32_t argb, uint32_t flags) {
    if (s_ncut == 0) {
        emit_poly(v, n, m, argb, flags);
        return;
    }
    paint_t p = {m, argb, flags};
    clip_subtract(&s_cut[0], v, n, 0, emit_cut0, &p);
}

// A quad, if it faces the eye, clipped to `cs` when there is one.
static void submit_quad(cvert_t const q[4], vec3_t n, cam_t const* cam, clipset_t const* cs, material_info_t const* m,
                        uint32_t argb, uint32_t flags) {
    if (v3_dot(v3_sub(cam->pos, q[0].p), n) <= 0.0f) return;
    if (cs == NULL) {
        emit(q, 4, m, argb, flags);
        return;
    }
    cvert_t   out[CLIP_MAX_VERTS];
    int const k = clip_polygon(cs, q, 4, out);
    if (k >= 3) emit(out, k, m, argb, flags);
}

static void submit_level(cam_t const* cam, clipset_t const* cs) {
    for (int i = 0; i < s_nquads; i++) {
        mquad_t const*         q    = &s_quads[i];
        material_info_t const* m    = &s_mat[q->mat];
        cvert_t const          v[4] = {
            {q->origin, 0, 0},
            {v3_add(q->origin, q->du), q->su, 0},
            {v3_add(v3_add(q->origin, q->du), q->dv), q->su, q->sv},
            {v3_add(q->origin, q->dv), 0, q->sv},
        };
        submit_quad(v, q->n, cam, cs, m, m->argb, m->flags);
    }
}

// A triangle on a portal's plane, `lift` off the wall.
static void portal_tri(portal_t const* p, vec3_t a, vec3_t b, vec3_t c, float lift, cam_t const* cam,
                       clipset_t const* cs, material_info_t const* m, uint32_t argb, uint32_t flags) {
    vec3_t const off = v3_scale(p->n, lift);
    cvert_t      v[4];
    vec3_t const w[3] = {v3_add(a, off), v3_add(b, off), v3_add(c, off)};
    // Texture coordinates from the world position along the face's two
    // in-plane axes: the same grid the level mesh tiles its panels on.
    int const    a_ = p->face / 2, ua = (a_ + 1) % 3, va = (a_ + 2) % 3;
    for (int i = 0; i < 3; i++) {
        float const k[3] = {w[i].x, w[i].y, w[i].z};
        v[i]             = (cvert_t){w[i], k[ua], k[va]};
    }
    if (v3_dot(v3_sub(cam->pos, w[0]), p->n) <= 0.0f) return;
    if (cs == NULL) {
        emit(v, 3, m, argb, flags);
        return;
    }
    cvert_t   out[CLIP_MAX_VERTS];
    int const k = clip_polygon(cs, v, 3, out);
    if (k >= 3) emit(out, k, m, argb, flags);
}

// The wall between the oval and the two cell faces the mesh left out:
// a fan from each corner of the rectangle over its quarter of the oval.
static void portal_frame(portal_t const* p, cam_t const* cam, clipset_t const* cs) {
    material_info_t const* m = &s_mat[MAT_WHITE];
    vec3_t                 o[PORTAL_OVAL_N];
    portal_oval(p, 1.0f, o);
    int const q = PORTAL_OVAL_N / 4;
    for (int k = 0; k < 4; k++) {
        float const  sr = (k == 0 || k == 3) ? 1.0f : -1.0f;
        float const  su = (k < 2) ? 1.0f : -1.0f;
        vec3_t const corner =
            v3_add(p->center, v3_add(v3_scale(p->right, sr * PORTAL_HALF_W), v3_scale(p->up, su * PORTAL_HALF_H)));
        for (int i = 0; i < q; i++)
            portal_tri(p, corner, o[k * q + i], o[(k * q + i + 1) % PORTAL_OVAL_N], 0.0f, cam, cs, m, m->argb,
                       m->flags);
    }
}

// The opening as a flat oval: a portal with nothing to show through it.
static void portal_disc(portal_t const* p, cam_t const* cam, clipset_t const* cs, uint32_t argb) {
    // Only to the rim's inner edge: overlapping it, the two were 2 mm
    // apart, less than one step of depth beyond a few metres, and the
    // rim flickered away.
    vec3_t o[PORTAL_OVAL_N];
    portal_oval(p, 0.92f, o);
    for (int i = 0; i < PORTAL_OVAL_N; i++)
        portal_tri(p, p->center, o[i], o[(i + 1) % PORTAL_OVAL_N], 0.002f, cam, cs, NULL, argb, SE_TRI_EMISSIVE);
}

// The coloured rim: it straddles the edge of the opening, hiding the
// pixel seam between the view and the wall round it.
static void portal_rim(portal_t const* p, int which, cam_t const* cam, clipset_t const* cs) {
    vec3_t in[PORTAL_OVAL_N], out[PORTAL_OVAL_N];
    portal_oval(p, 0.92f, in);
    portal_oval(p, 1.05f, out);
    uint32_t const c = s_rim[which];
    for (int i = 0; i < PORTAL_OVAL_N; i++) {
        int const j = (i + 1) % PORTAL_OVAL_N;
        portal_tri(p, in[i], out[i], out[j], 0.004f, cam, cs, NULL, c, SE_TRI_EMISSIVE);
        portal_tri(p, in[i], out[j], in[j], 0.004f, cam, cs, NULL, c, SE_TRI_EMISSIVE);
    }
}

// --- Things in the chamber ------------------------------------------------

// An axis-aligned box: the faces that face the eye, each textured 0..1.
static void submit_box(vec3_t lo, vec3_t hi, cam_t const* cam, clipset_t const* cs, material_info_t const* m,
                       uint32_t argb, uint32_t flags) {
    for (int face = 0; face < 6; face++) {
        int const   a = face / 2, ua = (a + 1) % 3, va = (a + 2) % 3;
        float const l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
        float       o[3] = {l[0], l[1], l[2]};
        o[a]             = face % 2 == 0 ? h[a] : l[a];
        float du[3] = {0}, dv[3] = {0};
        du[ua]             = h[ua] - l[ua];
        dv[va]             = h[va] - l[va];
        vec3_t const  p0   = v3(o[0], o[1], o[2]);
        vec3_t const  u    = v3(du[0], du[1], du[2]);
        vec3_t const  v    = v3(dv[0], dv[1], dv[2]);
        cvert_t const q[4] = {
            {p0, 0, 0}, {v3_add(p0, u), 1, 0}, {v3_add(v3_add(p0, u), v), 1, 1}, {v3_add(p0, v), 0, 1}};
        submit_quad(q, dir_vec(face), cam, cs, m, argb, flags);
    }
}

static void submit_things(game_t const* g, cam_t const* cam, clipset_t const* cs) {
    for (int i = 0; i < g->n_cubes; i++) {
        aabb_t const b = cube_aabb(&g->cubes[i]);
        submit_box(b.lo, b.hi, cam, cs, &s_cube, s_cube.argb, 0);
    }
    // The moving platform: a metal slab with a glowing edge.
    if (g->lv.n_platforms) {
        aabb_t const p = platform_aabb(g);
        submit_box(p.lo, p.hi, cam, cs, &s_mat[MAT_METAL], s_mat[MAT_METAL].argb, 0);
        submit_box(v3(p.lo.x, p.hi.y, p.lo.z), v3(p.hi.x, p.hi.y + 0.01f, p.lo.z + 0.06f), cam, cs, NULL, 0xFF2C8CFFu,
                   SE_TRI_EMISSIVE);
        submit_box(v3(p.lo.x, p.hi.y, p.hi.z - 0.06f), v3(p.hi.x, p.hi.y + 0.01f, p.hi.z), cam, cs, NULL, 0xFF2C8CFFu,
                   SE_TRI_EMISSIVE);
    }
    // Where a faith plate lands you: a faint orange square on the floor.
    for (int i = 0; i < g->lv.n_jumps; i++) {
        vec3_t const t = g->lv.jumps[i].target;
        submit_box(v3(t.x - 0.4f, t.y, t.z - 0.4f), v3(t.x + 0.4f, t.y + 0.02f, t.z + 0.4f), cam, cs, NULL, 0xFF8A4A10u,
                   SE_TRI_EMISSIVE);
    }
    for (int i = 0; i < g->lv.n_buttons; i++) {
        button_t const* bt = &g->lv.buttons[i];
        float const     x = (float)bt->x, z = (float)bt->z, top = (float)bt->y + 1.0f;
        submit_box(v3(x + 0.05f, top, z + 0.05f), v3(x + 0.95f, top + 0.04f, z + 0.95f), cam, cs, NULL, 0xFF5C6066u, 0);
        float const    h   = bt->pressed ? 0.06f : 0.12f;
        // Red for anyone, blue for a cube only.
        uint32_t const lit = bt->cube_only ? 0xFF40A0FFu : 0xFFFF6040u, off = bt->cube_only ? 0xFF1C4C90u : 0xFFB02818u;
        submit_box(v3(x + 0.2f, top, z + 0.2f), v3(x + 0.8f, top + h, z + 0.8f), cam, cs, NULL, bt->pressed ? lit : off,
                   bt->pressed ? SE_TRI_EMISSIVE : 0);
        // A pedestal button's time left: a blue bar along the pad that
        // shrinks as it runs out.
        if (bt->pedestal && bt->timer_left > 0.0f && g->lv.timer > 0.0f) {
            float const len = 0.8f * bt->timer_left / g->lv.timer;
            submit_box(v3(x + 0.1f, top, z + 0.06f), v3(x + 0.1f + len, top + 0.05f, z + 0.14f), cam, cs, NULL,
                       0xFF2C8CFFu, SE_TRI_EMISSIVE);
        }
    }
    // Droppers: a dark hatch under the ceiling cell, with a light round
    // its edge where the cube comes out.
    for (int i = 0; i < g->lv.n_cubes; i++) {
        if (!g->lv.cube_drop[i]) continue;
        vec3_t const c = g->lv.cubes[i];
        float const  x = floorf(c.x), z = floorf(c.z), y = c.y + LV_DROP_DEPTH;
        submit_box(v3(x + 0.1f, y - 0.02f, z + 0.1f), v3(x + 0.9f, y, z + 0.9f), cam, cs, NULL, 0xFF0C0D10u, 0);
        submit_box(v3(x + 0.05f, y - 0.03f, z + 0.05f), v3(x + 0.95f, y - 0.01f, z + 0.1f), cam, cs, NULL, 0xFFFF8A1Cu,
                   SE_TRI_EMISSIVE);
        submit_box(v3(x + 0.05f, y - 0.03f, z + 0.9f), v3(x + 0.95f, y - 0.01f, z + 0.95f), cam, cs, NULL, 0xFFFF8A1Cu,
                   SE_TRI_EMISSIVE);
    }
    // Doors: two panels, 0.2 thick in the middle of their cells, that
    // slide apart into the frame as the door opens, and a light across
    // the top, orange while shut and blue while open.
    for (int i = 0; i < g->lv.n_doors; i++) {
        door_t const* d       = &g->lv.doors[i];
        bool const    along_x = (d->z1 - d->z0) == 1;  // thin in z: the panels slide along x
        float const   a0 = along_x ? (float)d->x0 : (float)d->z0, a1 = along_x ? (float)d->x1 : (float)d->z1;
        float const   t0 = along_x ? (float)d->z0 + 0.4f : (float)d->x0 + 0.4f, t1 = t0 + 0.2f;
        float const   y0 = (float)d->y0, y1 = (float)d->y1;
        float const   half       = (a1 - a0) * 0.5f * (1.0f - d->open);
        float const   ends[2][2] = {{a0, a0 + half}, {a1 - half, a1}};
        for (int k = 0; k < 2; k++) {
            if (half < 0.01f) break;
            vec3_t const lo = along_x ? v3(ends[k][0], y0, t0) : v3(t0, y0, ends[k][0]);
            vec3_t const hi = along_x ? v3(ends[k][1], y1, t1) : v3(t1, y1, ends[k][1]);
            submit_box(lo, hi, cam, cs, NULL, 0xFF7A7E86u, 0);
        }
        uint32_t const light = d->open > 0.5f ? 0xFF2C8CFFu : 0xFFFF8A1Cu;
        vec3_t const   llo   = along_x ? v3(a0, y1 - 0.08f, t0 - 0.01f) : v3(t0 - 0.01f, y1 - 0.08f, a0);
        vec3_t const   lhi   = along_x ? v3(a1, y1, t1 + 0.01f) : v3(t1 + 0.01f, y1, a1);
        submit_box(llo, lhi, cam, cs, NULL, light, SE_TRI_EMISSIVE);
    }
}

static void set_camera(cam_t const* cam) {
    float yaw, pitch, roll;
    basis_to_angles(&cam->b, &yaw, &pitch, &roll);
    render_set_camera_6dof(cam->pos.x, cam->pos.y, cam->pos.z, yaw, pitch, roll);
}

// One pass: the chamber seen by `cam`, limited to `cs`. A portal whose
// bit is in `fill` gets a flat face instead of its opening.
static game_t const* s_game;  // the frame being drawn

static void draw_pass(pax_buf_t* target, cam_t const* cam, clipset_t const* cs, portal_t const portals[2], int fill,
                      int cut, uint32_t const fill_argb[2]) {
    scene_begin(target);
    set_camera(cam);
    se_light_set(&(se_light_t){.x = s_light.x, .y = s_light.y, .z = s_light.z, .brightness = 0.55f});
    s_ncut = 0;
    for (int i = 0; i < 2; i++)
        if (cut & (1 << i)) portal_behind(&portals[i], cam->pos, &s_cut[s_ncut++]);
    submit_level(cam, cs);
    submit_things(s_game, cam, cs);
    for (int i = 0; i < 2; i++) {
        if (!portals[i].open) continue;
        portal_frame(&portals[i], cam, cs);
        if (fill & (1 << i)) portal_disc(&portals[i], cam, cs, fill_argb[i]);
        portal_rim(&portals[i], i, cam, cs);
    }
    // Last: glass and fizzlers, which mix with what is behind them, so all
    // of that must be drawn first -- the depth-order pass (render_frame)
    // then puts them far to near. Not drawn at all without their texture:
    // the engine blends textured triangles only, and an opaque one would
    // look like a wall.
    for (int i = 0; i < s_nclear; i++) {
        mquad_t const*         q = &s_clear[i];
        material_info_t const* m = &s_mat[q->mat];
        if (m->tex == NULL) continue;
        cvert_t const v[4] = {
            {q->origin, 0, 0},
            {v3_add(q->origin, q->du), 1, 0},
            {v3_add(v3_add(q->origin, q->du), q->dv), 1, 1},
            {v3_add(q->origin, q->dv), 0, 1},
        };
        submit_quad(v, q->n, cam, cs, m, m->argb, m->flags);
    }
    scene_render(SE_RENDER_ZBUFFER);
    s_ncut = 0;
    s_stat_passes++;
}

// --- Through the portals ----------------------------------------------

// The four side planes of the camera's view, as a clip set. No near
// plane: an eye a hair from an opening it is stepping through must
// still count it as in view, or the frame shows what was there before.
static void view_frustum(cam_t const* cam, clipset_t* out) {
    float const  l = RENDER_HALF_W / RENDER_FOCAL_LEN;
    float const  r = ((float)DISPLAY_LOG_W - RENDER_HALF_W) / RENDER_FOCAL_LEN;
    float const  t = RENDER_HORIZON_Y / RENDER_FOCAL_LEN;
    float const  b = ((float)DISPLAY_LOG_H - RENDER_HORIZON_Y) / RENDER_FOCAL_LEN;
    vec3_t const f = cam->b.fwd, x = cam->b.right, y = cam->b.up;
    // Each normal points into the view.
    vec3_t const n[4] = {
        v3_norm(v3_add(f, v3_scale(x, 1.0f / l))),  // left edge
        v3_norm(v3_sub(f, v3_scale(x, 1.0f / r))),  // right
        v3_norm(v3_sub(f, v3_scale(y, 1.0f / t))),  // top
        v3_norm(v3_add(f, v3_scale(y, 1.0f / b))),  // bottom
    };
    out->n = 4;
    for (int i = 0; i < 4; i++) out->p[i] = (plane_t){n[i], -v3_dot(n[i], cam->pos)};
}

// Whether any of portal `p`'s opening is in view of `cam` within `cs`.
static bool portal_visible(portal_t const* p, cam_t const* cam, clipset_t const* cs) {
    // In front of it at all: an eye a millimetre from the plane, about to
    // go through, must still see the view, or the frame is left undrawn.
    if (v3_dot(v3_sub(cam->pos, p->center), p->n) <= 0.0f) return false;
    static clipset_t all;  // static: off the task's stack
    view_frustum(cam, &all);
    if (cs != NULL)
        for (int i = 0; i < cs->n && all.n < PORTAL_MAX_PLANES; i++) all.p[all.n++] = cs->p[i];
    vec3_t c[4];
    portal_corners(p, c);
    cvert_t const q[4] = {{c[0], 0, 0}, {c[1], 0, 0}, {c[2], 0, 0}, {c[3], 0, 0}};
    cvert_t       out[CLIP_MAX_VERTS];
    return clip_polygon(&all, q, 4, out) >= 3;
}

// Everything seen through portal `which` by `cam`, deepest view first.
static void draw_through(pax_buf_t* target, portal_t const portals[2], int which, cam_t const* cam,
                         clipset_t const* cs_in, int depth) {
    portal_t const*  in  = &portals[which];
    portal_t const*  out = &portals[which ^ 1];
    // One clip set per depth, static: three of these deep is a lot of stack.
    static clipset_t sets[RENDER_PORTAL_DEPTH_MAX + 1];
    clipset_t* const cs = &sets[depth];
    portal_clip_through(in, out, cam->pos, cs_in, cs);
    cam_t const v = {portal_map_point(in, out, cam->pos), portal_map_basis(in, out, &cam->b)};

    // From beyond `out` the only portal that can be in view is `in`.
    bool const deeper = depth + 1 < s_depth && portal_visible(in, &v, cs);
    if (deeper) draw_through(target, portals, which, &v, cs, depth + 1);
    draw_pass(target, &v, cs, portals, deeper ? 0 : 1 << which, deeper ? 1 << which : 0, s_deep);
}

void render_frame(pax_buf_t* target, game_t const* g) {
    portal_t const* portals = g->portals;
    s_game                  = g;
    s_stat_passes           = 0;
    s_stat_tris             = 0;
    cam_t const cam         = {player_eye(&g->pl), player_view(&g->pl)};

    // Glass and fizzlers blend, and only come out right drawn after
    // everything solid and far to near: the engine's depth-order pass.
    scene_set_options(&(se_scene_options_t){.frustum_cull = true, .depth_order = s_nclear > 0});

    int fill = 0, cut = 0;
    if (portals[0].open && portals[1].open) {
        // The farther portal's views first: where the two openings
        // overlap on screen, the nearer one is in front.
        int order[2] = {0, 1};
        if (v3_len(v3_sub(portals[1].center, cam.pos)) > v3_len(v3_sub(portals[0].center, cam.pos))) {
            order[0] = 1;
            order[1] = 0;
        }
        for (int k = 0; k < 2; k++) {
            if (!portal_visible(&portals[order[k]], &cam, NULL)) continue;
            draw_through(target, portals, order[k], &cam, NULL, 0);
            cut |= 1 << order[k];
        }
    } else {
        fill = (portals[0].open ? 1 : 0) | (portals[1].open ? 2 : 0);
    }
    draw_pass(target, &cam, NULL, portals, fill, cut, s_shut);
}
