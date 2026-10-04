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
    [MAT_WHITE] = {"white.png", 0xFFD8D8D0u, 0, NULL},
    [MAT_METAL] = {"metal.png", 0xFF44484Cu, 0, NULL},
    [MAT_GOO]   = {"goo.png", 0xFF5A4A18u, SE_TRI_EMISSIVE, NULL},
    [MAT_EXIT]  = {"exit.png", 0xFF30D060u, SE_TRI_EMISSIVE, NULL},
};

static uint32_t const s_rim[2]  = {0xFF2C8CFFu, 0xFFFF8A1Cu};
static uint32_t const s_shut[2] = {0xFF0C2850u, 0xFF502808u};  // one portal, nothing through it
static uint32_t const s_deep[2] = {0xFF184070u, 0xFF704018u};  // past the deepest view drawn

static mquad_t s_quads[LV_MAX_QUADS];
static int     s_nquads;
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
    scene_set_options(&(se_scene_options_t){.frustum_cull = true, .depth_order = false});
}

void render_set_level(level_t const* lv, portal_t const portals[2]) {
    hole_t holes[4];
    int    nh = 0;
    for (int i = 0; i < 2; i++) {
        if (!portals[i].open) continue;
        for (int c = 0; c < 2; c++)
            holes[nh++] = (hole_t){portals[i].cell[c][0], portals[i].cell[c][1], portals[i].cell[c][2], portals[i].face};
    }
    s_nquads = level_mesh(lv, holes, nh, s_quads, LV_MAX_QUADS);
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

// A quad, if it faces the eye, clipped to `cs` when there is one.
static void submit_quad(cvert_t const q[4], vec3_t n, cam_t const* cam, clipset_t const* cs,
                        material_info_t const* m, uint32_t argb, uint32_t flags) {
    if (v3_dot(v3_sub(cam->pos, q[0].p), n) <= 0.0f) return;
    if (cs == NULL) {
        emit_poly(q, 4, m, argb, flags);
        return;
    }
    cvert_t out[CLIP_MAX_VERTS];
    int const k = clip_polygon(cs, q, 4, out);
    if (k >= 3) emit_poly(out, k, m, argb, flags);
}

static void submit_level(cam_t const* cam, clipset_t const* cs) {
    for (int i = 0; i < s_nquads; i++) {
        mquad_t const*         q = &s_quads[i];
        material_info_t const* m = &s_mat[q->mat];
        cvert_t const v[4] = {
            {q->origin, 0, 0},
            {v3_add(q->origin, q->du), q->su, 0},
            {v3_add(v3_add(q->origin, q->du), q->dv), q->su, q->sv},
            {v3_add(q->origin, q->dv), 0, q->sv},
        };
        submit_quad(v, q->n, cam, cs, m, m->argb, m->flags);
    }
}

// A rectangle on portal `p`'s plane, from (r0, u0) to (r1, u1) in its
// frame, lifted `lift` off the wall.
static void portal_rect(portal_t const* p, float r0, float u0, float r1, float u1, float lift, cam_t const* cam,
                        clipset_t const* cs, uint32_t argb) {
    vec3_t const  c = v3_mad(p->center, p->n, lift);
    cvert_t const v[4] = {
        {v3_add(v3_scale(p->right, r0), v3_add(c, v3_scale(p->up, u0))), 0, 0},
        {v3_add(v3_scale(p->right, r1), v3_add(c, v3_scale(p->up, u0))), 0, 0},
        {v3_add(v3_scale(p->right, r1), v3_add(c, v3_scale(p->up, u1))), 0, 0},
        {v3_add(v3_scale(p->right, r0), v3_add(c, v3_scale(p->up, u1))), 0, 0},
    };
    submit_quad(v, p->n, cam, cs, NULL, argb, SE_TRI_EMISSIVE);
}

// The coloured rim: it straddles the edge of the opening, hiding the
// pixel seam between the view and the wall round it.
static void portal_rim(portal_t const* p, int which, cam_t const* cam, clipset_t const* cs) {
    float const w = PORTAL_HALF_W, h = PORTAL_HALF_H, o = 0.04f, i = 0.07f, l = 0.004f;
    uint32_t const c = s_rim[which];
    portal_rect(p, -w - o, -h - o, w + o, -h + i, l, cam, cs, c);
    portal_rect(p, -w - o, h - i, w + o, h + o, l, cam, cs, c);
    portal_rect(p, -w - o, -h + i, -w + i, h - i, l, cam, cs, c);
    portal_rect(p, w - i, -h + i, w + o, h - i, l, cam, cs, c);
}

static void set_camera(cam_t const* cam) {
    float yaw, pitch, roll;
    basis_to_angles(&cam->b, &yaw, &pitch, &roll);
    render_set_camera_6dof(cam->pos.x, cam->pos.y, cam->pos.z, yaw, pitch, roll);
}

// One pass: the chamber seen by `cam`, limited to `cs`. A portal whose
// bit is in `fill` gets a flat face instead of its opening.
static void draw_pass(pax_buf_t* target, cam_t const* cam, clipset_t const* cs, portal_t const portals[2],
                      int fill, uint32_t const fill_argb[2]) {
    scene_begin(target);
    set_camera(cam);
    se_light_set(&(se_light_t){.x = s_light.x, .y = s_light.y, .z = s_light.z, .brightness = 0.55f});
    submit_level(cam, cs);
    for (int i = 0; i < 2; i++) {
        if (!portals[i].open) continue;
        if (fill & (1 << i)) portal_rect(&portals[i], -PORTAL_HALF_W, -PORTAL_HALF_H, PORTAL_HALF_W, PORTAL_HALF_H,
                                         0.002f, cam, cs, fill_argb[i]);
        portal_rim(&portals[i], i, cam, cs);
    }
    scene_render(SE_RENDER_ZBUFFER);
    s_stat_passes++;
}

// --- Through the portals ----------------------------------------------

// The four side planes of the camera's view, as a clip set. No near
// plane: an eye a hair from an opening it is stepping through must
// still count it as in view, or the frame shows what was there before.
static void view_frustum(cam_t const* cam, clipset_t* out) {
    float const l = RENDER_HALF_W / RENDER_FOCAL_LEN;
    float const r = ((float)DISPLAY_LOG_W - RENDER_HALF_W) / RENDER_FOCAL_LEN;
    float const t = RENDER_HORIZON_Y / RENDER_FOCAL_LEN;
    float const b = ((float)DISPLAY_LOG_H - RENDER_HORIZON_Y) / RENDER_FOCAL_LEN;
    vec3_t const f = cam->b.fwd, x = cam->b.right, y = cam->b.up;
    // Each normal points into the view.
    vec3_t const n[4] = {
        v3_norm(v3_add(f, v3_scale(x, 1.0f / l))),   // left edge
        v3_norm(v3_sub(f, v3_scale(x, 1.0f / r))),   // right
        v3_norm(v3_sub(f, v3_scale(y, 1.0f / t))),   // top
        v3_norm(v3_add(f, v3_scale(y, 1.0f / b))),   // bottom
    };
    out->n = 4;
    for (int i = 0; i < 4; i++) out->p[i] = (plane_t){n[i], -v3_dot(n[i], cam->pos)};
}

// Whether any of portal `p`'s opening is in view of `cam` within `cs`.
static bool portal_visible(portal_t const* p, cam_t const* cam, clipset_t const* cs) {
    if (v3_dot(v3_sub(cam->pos, p->center), p->n) <= 0.001f) return false;
    clipset_t all;
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
    portal_t const* in  = &portals[which];
    portal_t const* out = &portals[which ^ 1];
    clipset_t       cs;
    portal_clip_through(in, out, cam->pos, cs_in, &cs);
    cam_t const v = {portal_map_point(in, out, cam->pos), portal_map_basis(in, out, &cam->b)};

    // From beyond `out` the only portal that can be in view is `in`.
    bool const deeper = depth + 1 < s_depth && portal_visible(in, &v, &cs);
    if (deeper) draw_through(target, portals, which, &v, &cs, depth + 1);
    draw_pass(target, &v, &cs, portals, deeper ? 0 : 1 << which, s_deep);
}

void render_frame(pax_buf_t* target, level_t const* lv, player_t const* pl, portal_t const portals[2]) {
    (void)lv;
    s_stat_passes = 0;
    s_stat_tris   = 0;
    cam_t const cam = {player_eye(pl), player_view(pl)};

    int fill = 0;
    if (portals[0].open && portals[1].open) {
        // The farther portal's views first: where the two openings
        // overlap on screen, the nearer one is in front.
        int order[2] = {0, 1};
        if (v3_len(v3_sub(portals[1].center, cam.pos)) > v3_len(v3_sub(portals[0].center, cam.pos))) {
            order[0] = 1;
            order[1] = 0;
        }
        for (int k = 0; k < 2; k++)
            if (portal_visible(&portals[order[k]], &cam, NULL)) draw_through(target, portals, order[k], &cam, NULL, 0);
    } else {
        fill = (portals[0].open ? 1 : 0) | (portals[1].open ? 2 : 0);
    }
    draw_pass(target, &cam, NULL, portals, fill, s_shut);
}
