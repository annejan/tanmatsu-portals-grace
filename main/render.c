#include "render.h"
#include <stdio.h>
#include <string.h>
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
    [MAT_WHITE]        = {"white.png", 0xFFD8D8D0u, 0, NULL},
    [MAT_METAL]        = {"metal.png", 0xFF44484Cu, 0, NULL},
    [MAT_GOO]          = {"goo.png", 0xFF5A4A18u, SE_TRI_EMISSIVE, NULL},
    [MAT_EXIT]         = {"exit.png", 0xFF30D060u, SE_TRI_EMISSIVE, NULL},
    [MAT_GLASS]        = {"glass.png", 0xFF9ED8F0u, SE_TRI_BLEND, NULL},
    [MAT_FIZZ]         = {"fizz.png", 0xFF60B0FFu, SE_TRI_BLEND | SE_TRI_EMISSIVE, NULL},
    [MAT_JUMP]         = {"jump.png", 0xFFE08020u, 0, NULL},
    // Flat colours: the pedestal a light grey block, the dropper dark.
    [MAT_PEDESTAL]     = {NULL, 0xFF7A7E86u, 0, NULL},
    [MAT_DROPPER]      = {NULL, 0xFF34363Bu, 0, NULL},
    [MAT_CUBEBASE]     = {NULL, 0xFF3E4A60u, 0, NULL},
    [MAT_EMITTER]      = {NULL, 0xFF5A2A2Au, 0, NULL},
    [MAT_CATCHER]      = {NULL, 0xFF6A5030u, 0, NULL},
    [MAT_BRIDGE]       = {NULL, 0xFF2A4A6Au, 0, NULL},
    [MAT_DISP_BLUE]    = {NULL, 0xFF1E3E78u, 0, NULL},
    [MAT_DISP_ORANGE]  = {NULL, 0xFF784012u, 0, NULL},
    [MAT_DISP_WHITE]   = {NULL, 0xFF8A8A84u, 0, NULL},
    [MAT_LAUNCHER]     = {NULL, 0xFF4A4038u, 0, NULL},
    [MAT_RECEIVER]     = {NULL, 0xFF50543Au, 0, NULL},
    [MAT_RELAY]        = {NULL, 0xFF8A8E96u, 0, NULL},
    [MAT_FIELD]        = {"field.png", 0xFFFF4030u, SE_TRI_BLEND | SE_TRI_EMISSIVE, NULL},
    [MAT_CUP]          = {NULL, 0xFF2E4A5Cu, 0, NULL},
    [MAT_FUNNEL]       = {NULL, 0xFF2A3E5Cu, 0, NULL},
    [MAT_PAINT_BLUE]   = {NULL, 0xFF2E7BFFu, 0, NULL},
    [MAT_PAINT_ORANGE] = {NULL, 0xFFFF8A1Cu, 0, NULL},
    [MAT_PAINT_WHITE]  = {"white.png", 0xFFE8E8E2u, 0, NULL},
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
static float   s_time;  // render_set_time()

// Texture drift, in texture widths a second: the goo slowly sideways, a
// fizzler's streaks falling.
#define GOO_DRIFT_U 0.05f
#define GOO_DRIFT_V 0.03f
#define FIZZ_FALL   0.9f

void render_set_time(float seconds) {
    s_time = seconds;
}

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
                        if (!level_open(nb)) continue;
                        int const a = face / 2, ua = (a + 1) % 3, va = (a + 2) % 3;
                        float     o[3] = {(float)x, (float)y, (float)z}, du[3] = {0}, dv[3] = {0};
                        if (face % 2 == 0) o[a] += 1.0f;
                        du[ua] = 1.0f;
                        dv[va] = 1.0f;
                        add_clear(v3(o[0], o[1], o[2]), v3(du[0], du[1], du[2]), v3(dv[0], dv[1], dv[2]), dir_vec(face),
                                  m);
                    }
                } else if (level_sheet(m)) {
                    // A sheet through the middle of the cell, across the
                    // way the fizzler (or laser field) runs, seen from both sides.
                    bool along_x = level_get(lv, x - 1, y, z) == m || level_get(lv, x + 1, y, z) == m ||
                                   !(level_get(lv, x, y, z - 1) == m || level_get(lv, x, y, z + 1) == m);
            // Across a corridor: open on both ends along one axis only.
#define CLEAR(dx, dz) \
    (level_open(level_get(lv, x + (dx), y, z + (dz))) && !level_sheet(level_get(lv, x + (dx), y, z + (dz))))
                    bool const open_x = CLEAR(-1, 0) && CLEAR(1, 0), open_z = CLEAR(0, -1) && CLEAR(0, 1);
#undef CLEAR
                    if (open_x && !open_z) along_x = false;
                    if (open_z && !open_x) along_x = true;
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

// --- The mesh, slice by slice ---------------------------------------------
//
// Meshing a whole chamber takes time in proportion to its volume: a
// 64 x 32 x 64 one took 270 ms on the badge, at every portal shot. So
// each slice's rectangles are kept (s_quads holds them in level_mesh()'s
// order, slice after slice), and only the slices a change touches are
// meshed again: a portal's old and new holes, and the cells gel has
// painted since. A chamber whose cells differ is meshed whole.

#define SLICES_MAX (2 * (LV_MAX_W + LV_MAX_H + LV_MAX_D))
#define CELLS_MAX  (LV_MAX_W * LV_MAX_H * LV_MAX_D)

static int16_t s_slice_at[SLICES_MAX], s_slice_n[SLICES_MAX];     // each slice's rectangles, in s_quads
static uint8_t s_mesh_cells[CELLS_MAX], s_mesh_paint[CELLS_MAX];  // the chamber they were meshed from
static int     s_mesh_dims[3];
static hole_t  s_mesh_holes[4];
static int     s_mesh_nh = -1;  // -1: nothing meshed yet
static bool    s_dirty[SLICES_MAX];

// The slice `face` (DIR_*) / `s`: its index, faces in level_mesh()'s order.
static int slice_of(int const dims[3], int face, int s) {
    int k = 0;
    for (int f = 0; f < face; f++) k += dims[f / 2];
    return k + s;
}

// The six slices through cell c: each face of it.
static void dirty_cell(int const dims[3], int const c[3]) {
    for (int f = 0; f < 6; f++) s_dirty[slice_of(dims, f, c[f / 2])] = true;
}

// The mesh brought up to date with `lv` and its holes; true if the
// chamber itself is another (meshed whole). A portal shot costs only its
// slices: the cells are compared only when the level's serial changes,
// and the paint through its log of painted cells.
static uint32_t s_mesh_serial, s_mesh_paint_n;
static bool     remesh(level_t const* lv, hole_t const* holes, int nh) {
    int const    dims[3] = {lv->w, lv->h, lv->d};
    size_t const cells   = (size_t)lv->w * (size_t)lv->h * (size_t)lv->d;
    int const    slices  = 2 * (lv->w + lv->h + lv->d);
    bool const   fresh_l = s_mesh_nh < 0 || lv->serial != s_mesh_serial || memcmp(dims, s_mesh_dims, sizeof(dims)) != 0;
    bool const   whole   = fresh_l && (s_mesh_nh < 0 || memcmp(dims, s_mesh_dims, sizeof(dims)) != 0 ||
                                       memcmp(lv->cells, s_mesh_cells, cells) != 0);
    memset(s_dirty, whole, (size_t)slices);
    if (!whole) {
        // The holes that came or went: their slices.
        bool const same = nh == s_mesh_nh && memcmp(holes, s_mesh_holes, (size_t)nh * sizeof(hole_t)) == 0;
        for (int i = 0; !same && i < nh + s_mesh_nh; i++) {
            hole_t const* const h    = i < nh ? &holes[i] : &s_mesh_holes[i - nh];
            int const           c[3] = {h->x, h->y, h->z};
            if (h->face >= 0 && h->face < 6 && c[h->face / 2] >= 0 && c[h->face / 2] < dims[h->face / 2])
                s_dirty[slice_of(dims, h->face, c[h->face / 2])] = true;
        }
        // The cells gel has painted: from the log, or -- the same cells
        // made anew (a restart), the log run past or gone back -- all of
        // them compared. (Cells are kept x fastest, then z, then y:
        // level.c's idx().)
        uint32_t const since = lv->paint_n - s_mesh_paint_n;
        if (fresh_l || lv->paint_n < s_mesh_paint_n || since > LV_PAINT_LOG) {
            if (memcmp(lv->paint, s_mesh_paint, cells) != 0)
                for (size_t i = 0; i < cells; i++)
                    if (lv->paint[i] != s_mesh_paint[i]) {
                        int const c[3] = {(int)(i % (size_t)lv->w), (int)(i / ((size_t)lv->w * (size_t)lv->d)),
                                          (int)(i / (size_t)lv->w % (size_t)lv->d)};
                        dirty_cell(dims, c);
                    }
            memcpy(s_mesh_paint, lv->paint, cells);
        } else {
            for (uint32_t k = s_mesh_paint_n; k != lv->paint_n; k++) {
                size_t const i    = lv->paint_log[k % LV_PAINT_LOG];
                int const    c[3] = {(int)(i % (size_t)lv->w), (int)(i / ((size_t)lv->w * (size_t)lv->d)),
                                     (int)(i / (size_t)lv->w % (size_t)lv->d)};
                dirty_cell(dims, c);
                s_mesh_paint[i] = lv->paint[i];
            }
        }
    }
    // The map's edge, where an open cell meets it (level_mesh_border): it
    // changes only with the whole level.
#define BORDER_MAX 512
    static mquad_t border[6][BORDER_MAX];
    static int     border_n[6];
    if (whole)
        for (int face = 0; face < 6; face++) {
            border_n[face] = level_mesh_border(lv, face, border[face], BORDER_MAX);
            if (border_n[face] > BORDER_MAX) border_n[face] = BORDER_MAX;
        }
    // The new mesh: the dirty slices meshed, the rest as they were; each
    // face's border after its slices, as level_mesh() gives them.
    static mquad_t fresh[LV_MAX_QUADS];
    int            n = 0, k = 0;
    for (int face = 0; face < 6; face++) {
        for (int s = 0; s < dims[face / 2]; s++, k++) {
            int got;
            if (s_dirty[k]) {
                got = level_mesh_slice(lv, holes, nh, face, s, fresh + n, LV_MAX_QUADS - n);
                if (got > LV_MAX_QUADS - n) got = LV_MAX_QUADS - n;  // the parser refuses such chambers
            } else {
                got = s_slice_n[k];
                memcpy(fresh + n, s_quads + s_slice_at[k], (size_t)got * sizeof(mquad_t));
            }
            s_slice_at[k]  = (int16_t)n;
            s_slice_n[k]   = (int16_t)got;
            n             += got;
        }
        int const b = border_n[face] < LV_MAX_QUADS - n ? border_n[face] : LV_MAX_QUADS - n;
        memcpy(fresh + n, border[face], (size_t)b * sizeof(mquad_t));
        n += b;
    }
    memcpy(s_quads, fresh, (size_t)n * sizeof(mquad_t));
    s_nquads = n;
    if (whole) {
        memcpy(s_mesh_cells, lv->cells, cells);
        memcpy(s_mesh_paint, lv->paint, cells);
    }
    memcpy(s_mesh_dims, dims, sizeof(dims));
    memcpy(s_mesh_holes, holes, (size_t)nh * sizeof(hole_t));
    s_mesh_nh      = nh;
    s_mesh_serial  = lv->serial;
    s_mesh_paint_n = lv->paint_n;
    return whole;
}

int render_quads(mquad_t const** quads) {
    *quads = s_quads;
    return s_nquads;
}

static int      s_last_shot;  // the portal last moved: Chell's gun glows its colour
static portal_t s_prev[2];

void render_set_level(level_t const* lv, portal_t const portals[2]) {
    {
        // The one portal that moved since last time is the last shot.
        int moved = 0;
        for (int i = 0; i < 2; i++)
            if (portals[i].open && (!s_prev[i].open || v3_len(v3_sub(portals[i].center, s_prev[i].center)) > 1e-3f ||
                                    portals[i].face != s_prev[i].face))
                moved |= 1 << i;
        if (moved == 1 || moved == 2) s_last_shot = moved >> 1;
        s_prev[0] = portals[0];
        s_prev[1] = portals[1];
    }
    hole_t holes[4];
    int    nh = 0;
    for (int i = 0; i < 2; i++) {
        if (!portals[i].open) continue;
        for (int c = 0; c < 2; c++)
            holes[nh++] =
                (hole_t){portals[i].cell[c][0], portals[i].cell[c][1], portals[i].cell[c][2], portals[i].face};
    }
    if (remesh(lv, holes, nh)) {
        build_clear(lv);
        // Far off, so it is a direction: the engine lights each triangle on
        // its own, and a near light shades the two halves of a big merged
        // quad differently, leaving a seam along the diagonal.
        vec3_t const centre = v3((float)lv->w * 0.5f, (float)lv->h * 0.5f, (float)lv->d * 0.5f);
        s_light             = v3_mad(centre, v3_norm(v3(0.35f, 1.0f, -0.55f)), 5000.0f);
    }
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

// Decals: a ring or a lens on a face, a light on a door, a few mm off
// their surface. The badge keeps depth as 1/z in 16 bits, DEPTH_PER_M
// steps to 1/m -- a step is z^2 / 3200 m, 8 mm at 5 m, 3 cm at 10 m --
// and a tie goes to the triangle drawn first: closer than a step, a
// decal flickers with what it lies on. So each is drawn DECAL_STEPS
// steps nearer for each layer it lies on, DECAL(1) on a surface and
// DECAL(2) on another decal: slid toward the eye, on the same pixels.
#define DEPTH_PER_M  (64000.0f * RENDER_NEAR_CLIP_Z)  // the engine's SCENE_DEPTH_SCALE
#define DECAL_STEPS  2.0f
#define DECAL_SHIFT  30  // flag bits of our own, above the engine's: taken off before it sees them
#define DECAL(layer) ((uint32_t)(layer) << DECAL_SHIFT)
// Another of our own: a level quad whose glow submit_level has worked out
// already, lit or not, which submit_poly must not light again as a whole.
#define LIT          (1u << 29)

static cam_t s_eye;  // the pass's camera (set_camera)

// p, slid toward the eye until its 1/z is `steps` steps more.
static vec3_t nearer(vec3_t p, float steps) {
    vec3_t const d = v3_sub(p, s_eye.pos);
    return v3_mad(s_eye.pos, d, 1.0f / (1.0f + steps * v3_dot(d, s_eye.b.fwd) / DEPTH_PER_M));
}

static void emit_poly(cvert_t const* v, int n, material_info_t const* m, uint32_t argb, uint32_t flags) {
    float const lift  = DECAL_STEPS * (float)(flags >> DECAL_SHIFT);
    flags            &= ~(DECAL(3) | LIT);
    for (int i = 1; i + 1 < n; i++) {
        cvert_t t[3] = {v[0], v[i], v[i + 1]};
        if (lift > 0.0f)
            for (int k = 0; k < 3; k++) t[k].p = nearer(t[k].p, lift);
        if (m != NULL && m->tex != NULL) {
            se_tex_vertex_t const tv[3] = {
                {t[0].p.x, t[0].p.y, t[0].p.z, t[0].u, t[0].v},
                {t[1].p.x, t[1].p.y, t[1].p.z, t[1].u, t[1].v},
                {t[2].p.x, t[2].p.y, t[2].p.z, t[2].u, t[2].v},
            };
            scene_textured_tri(tv, m->tex, flags);
        } else {
            scene_tri(t[0].p.x, t[0].p.y, t[0].p.z, t[1].p.x, t[1].p.y, t[1].p.z, t[2].p.x, t[2].p.y, t[2].p.z, argb,
                      flags);
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

// While set, every polygon is carried in through s_via[0] and out of
// s_via[1] before it is drawn: the copy of a cube part way through a
// portal (submit_things).
static portal_t const* s_via[2];

// And while it has a plane, every polygon is cut to its front: each copy
// of a thing part way through a portal stays on its own portal's side.
static clipset_t s_keep;

static void keep_front(portal_t const* p) {
    s_keep.n    = 1;
    s_keep.p[0] = (plane_t){p->n, 0.002f - v3_dot(p->n, p->center)};
}

// A quad, if it faces the eye, clipped to `cs` when there is one.
// A convex polygon of `n` corners facing `n`ormal: dropped if it faces
// away from the eye, clipped to `cs`, and drawn.
// Ghosts (ghost.h) are drawn as Chell is, each in a glow of its own colour:
// her shading kept, as a tint of it.
#define GHOSTS_MAX 4
static player_t s_ghost_pl[GHOSTS_MAX];
static uint8_t  s_ghost_tint[GHOSTS_MAX];
static int      s_ghost_n;
static int      s_ghostly = -1;  // drawing ghost tint s_ghostly, or -1
static cam_t    s_view;          // the eye of the last frame drawn, for render_to_screen

static uint8_t const s_tints[][3] = {{60, 230, 255}, {255, 90, 220}, {255, 220, 60}, {120, 255, 110}};

static uint32_t ghostly(uint32_t argb, int tint) {
    uint32_t const r = (argb >> 16) & 255, g = (argb >> 8) & 255, b = argb & 255;
    uint32_t const l = (r * 3 + g * 6 + b) / 10;
    uint8_t const* t = s_tints[tint % 4];
    uint32_t const k = 90 + l * 165 / 255;  // 0.35 .. 1 of the tint
    return 0xFF000000u | ((t[0] * k / 255) << 16) | ((t[1] * k / 255) << 8) | (t[2] * k / 255);
}

void render_set_ghosts(player_t const* pls, uint8_t const* tints, int n) {
    s_ghost_n = n < GHOSTS_MAX ? n : GHOSTS_MAX;
    for (int i = 0; i < s_ghost_n; i++) {
        s_ghost_pl[i]   = pls[i];
        s_ghost_tint[i] = tints[i];
    }
}

bool render_to_screen(vec3_t p, float* sx, float* sy) {
    vec3_t const d  = v3_sub(p, s_view.pos);
    float const  cz = v3_dot(d, s_view.b.fwd);
    if (cz < 0.3f) return false;
    *sx = RENDER_HALF_W + RENDER_FOCAL_LEN * v3_dot(d, s_view.b.right) / cz;
    *sy = RENDER_HORIZON_Y - RENDER_FOCAL_LEN * v3_dot(d, s_view.b.up) / cz;
    return true;
}

// What glows lights what is round it: below (glow_gather).
static int      s_glow_n;
static uint32_t glow_at(vec3_t c, vec3_t n);

static void submit_poly(cvert_t const* q, int nq, vec3_t n, cam_t const* cam, clipset_t const* cs,
                        material_info_t const* m, uint32_t argb, uint32_t flags) {
    static cvert_t moved[CLIP_MAX_VERTS], kept[CLIP_MAX_VERTS];  // static: off the task's stack
    if (s_via[0] != NULL) {
        for (int i = 0; i < nq; i++) moved[i] = (cvert_t){portal_map_point(s_via[0], s_via[1], q[i].p), q[i].u, q[i].v};
        q = moved;
        n = portal_map_dir(s_via[0], s_via[1], n);
    }
    if (v3_dot(v3_sub(cam->pos, q[0].p), n) <= 0.0f) return;
    // A ghost: its own colour, glowing, lit by nothing.
    if (s_ghostly >= 0) {
        argb  = ghostly(argb, s_ghostly);
        flags = (flags & ~SE_TRI_GLOW_MASK) | SE_TRI_EMISSIVE;
    }
    // Lit by what glows (glow_at): a thing's face, by its middle. The
    // level's quads have theirs worked out already (LIT).
    if (s_glow_n > 0 && !(flags & (LIT | SE_TRI_EMISSIVE | SE_TRI_BLEND | SE_TRI_GLOW_MASK))) {
        vec3_t c = q[0].p;
        for (int i = 1; i < nq; i++) c = v3_add(c, q[i].p);
        flags |= SE_TRI_GLOW(glow_at(v3_scale(c, 1.0f / (float)nq), n));
    }
    if (s_keep.n) {
        nq = clip_polygon(&s_keep, q, nq, kept);
        if (nq < 3) return;
        q = kept;
    }
    if (cs == NULL) {
        emit(q, nq, m, argb, flags);
        return;
    }
    cvert_t   out[CLIP_MAX_VERTS];
    int const k = clip_polygon(cs, q, nq, out);
    if (k >= 3) emit(out, k, m, argb, flags);
}

static void submit_quad(cvert_t const q[4], vec3_t n, cam_t const* cam, clipset_t const* cs, material_info_t const* m,
                        uint32_t argb, uint32_t flags) {
    submit_poly(q, 4, n, cam, cs, m, argb, flags);
}

// --- Glow: what shines lights what is round it ---------------------------
//
// The engine's one light is the sun, far off. What glows in a chamber --
// a pellet in flight, a launcher charged with one, a receiver that has
// caught one -- is a light of the game's own (SE_TRI_GLOW): so much light
// added to each face near it, by how far off it is and how squarely the
// face meets it. A face's halves get one value, so nothing splits down a
// diagonal; near a light the big merged quads are cut into cells, each
// lit on its own, for a pool of light rather than a lit wall.
//
// Light goes through portals as everything else does: a light near an
// open portal shines out of the other one too, as if from behind it --
// on what is in front of that portal only.

#define GLOW_LIGHTS 16

typedef struct {
    vec3_t at;
    float  radius;  // m: past this it lights nothing
    float  peak;    // a face right beside it, square on; past SE_TRI_GLOW_MAX is held there
    bool   bound;   // through a portal: it lights only what it can reach through that portal's opening
    vec3_t pn;      // ... the portal's plane: pn.x + pd > 0 in front of it
    float  pd;
    vec3_t oc, orr, oup;  // ... and its opening: centre, right, up
} glow_t;

static glow_t s_glow[GLOW_LIGHTS];
static int    s_glow_n;

static void glow_add(vec3_t at, float radius, float peak) {
    if (s_glow_n < GLOW_LIGHTS) s_glow[s_glow_n++] = (glow_t){.at = at, .radius = radius, .peak = peak};
}

// This frame's lights, from what glows in `g`.
static void glow_gather(game_t const* g) {
    s_glow_n = 0;
    for (int k = 0; k < g->lv.n_launchers; k++) {
        pellet_t const* p = &g->pellets[k];
        if (p->live) glow_add(p->pos, 4.5f, 44.0f);
        // The launcher, charged: its mouth glows until its pellet is caught.
        emitter_t const* L = &g->lv.launchers[k];
        vec3_t const     n = dir_vec(L->dir);
        if (!p->done) glow_add(v3_mad(v3((float)L->x + 0.5f, (float)L->y + 0.5f, (float)L->z + 0.5f), n, 0.75f), 2.5f, 20.0f);
    }
    // Receivers that have caught theirs: a lit lens on each open side.
    for (int i = 0; i < g->lv.n_buttons; i++) {
        button_t const* bt = &g->lv.buttons[i];
        if (!bt->receiver || !bt->pressed) continue;
        vec3_t const mid = v3((float)bt->x + 0.5f, (float)bt->y + 0.5f, (float)bt->z + 0.5f);
        for (int face = 0; face < 6; face++) {
            if (face == DIR_PY || face == DIR_NY) continue;
            int dx, dy, dz;
            dir_step(face, &dx, &dy, &dz);
            if (!level_solid(&g->lv, bt->x + dx, bt->y + dy, bt->z + dz))
                glow_add(v3_mad(mid, dir_vec(face), 0.75f), 2.5f, 20.0f);
        }
    }
    // And each, near an open portal, out of the other one.
    portal_t const* pt = g->portals;
    if (!pt[0].open || !pt[1].open) return;
    int const n0 = s_glow_n;
    for (int i = 0; i < n0; i++)
        for (int a = 0; a < 2; a++) {
            portal_t const* in  = &pt[a];
            portal_t const* out = &pt[a ^ 1];
            vec3_t const    d   = v3_sub(s_glow[i].at, in->center);
            float const     h   = v3_dot(d, in->n);  // in front of the opening, and near it
            if (h <= 0.0f || v3_len(d) > s_glow[i].radius + PORTAL_HALF_H || s_glow_n >= GLOW_LIGHTS) continue;
            s_glow[s_glow_n++] = (glow_t){.at     = portal_map_point(in, out, s_glow[i].at),
                                          .radius = s_glow[i].radius,
                                          .peak   = s_glow[i].peak,
                                          .bound  = true,
                                          .pn     = out->n,
                                          .pd     = -v3_dot(out->n, out->center),
                                          .oc     = out->center,
                                          .orr    = out->right,
                                          .oup    = out->up};
        }
}

// Whether light `l` gets to `c`: always, unless it shines out of a portal
// -- then only in front of it, and only along a line through the opening
// (a little wider, so the pool's edge stays soft).
static bool glow_reaches(glow_t const* l, vec3_t c) {
    if (!l->bound) return true;
    float const hc = v3_dot(l->pn, c) + l->pd;
    if (hc <= 0.02f) return false;
    float const  ha  = v3_dot(l->pn, l->at) + l->pd;  // < 0: behind the portal
    vec3_t const hit = v3_add(l->at, v3_scale(v3_sub(c, l->at), ha / (ha - hc)));
    vec3_t const off = v3_sub(hit, l->oc);
    return fabsf(v3_dot(off, l->orr)) < PORTAL_HALF_W + 0.4f && fabsf(v3_dot(off, l->oup)) < PORTAL_HALF_H + 0.4f;
}

// Whether light `l` gets to the front of `c` at all: the plane alone, the
// quick test (a big quad's corners may all be outside the light's cone
// through the opening, and cells in its middle inside it).
static bool glow_front(glow_t const* l, vec3_t c) {
    return !l->bound || v3_dot(l->pn, c) + l->pd > 0.02f;
}

// How far from light `l` a face square on to it still gets a glow of one.
static float glow_reach(glow_t const* l) {
    return l->radius * (1.0f - sqrtf(0.5f / l->peak));
}

// The glow on a face at `c`, facing `n`: 0..SE_TRI_GLOW_MAX.
static uint32_t glow_at(vec3_t c, vec3_t n) {
    float g = 0.0f;
    for (int i = 0; i < s_glow_n; i++) {
        glow_t const* l    = &s_glow[i];
        vec3_t const  d    = v3_sub(l->at, c);
        float const   dist = v3_len(d);
        if (dist >= l->radius || !glow_reaches(l, c)) continue;
        float const facing = dist > 1e-3f ? v3_dot(n, d) / dist : 1.0f;
        if (facing <= 0.0f) continue;  // behind the face
        float const fall  = 1.0f - dist / l->radius;
        g                += l->peak * fall * fall * (0.35f + 0.65f * facing);
    }
    return g >= (float)SE_TRI_GLOW_MAX ? SE_TRI_GLOW_MAX : (uint32_t)(g + 0.5f);
}

// Part of quad `q`: its fractions [a0, a1] along du and [b0, b1] along dv.
static void submit_part(mquad_t const* q, float a0, float a1, float b0, float b1, float ou, float ov, cam_t const* cam,
                        clipset_t const* cs, material_info_t const* m, uint32_t flags) {
    vec3_t const  p00 = v3_add(q->origin, v3_add(v3_scale(q->du, a0), v3_scale(q->dv, b0)));
    vec3_t const  du  = v3_scale(q->du, a1 - a0), dv = v3_scale(q->dv, b1 - b0);
    cvert_t const v[4] = {
        {p00, q->su * a0 + ou, q->sv * b0 + ov},
        {v3_add(p00, du), q->su * a1 + ou, q->sv * b0 + ov},
        {v3_add(v3_add(p00, du), dv), q->su * a1 + ou, q->sv * b1 + ov},
        {v3_add(p00, dv), q->su * a0 + ou, q->sv * b1 + ov},
    };
    submit_quad(v, q->n, cam, cs, m, m->argb, flags);
}

// Whether light `l` reaches quad `q` at all -- in front of it, and within
// its radius of the quad -- and if so, the cells it reaches.
static bool glow_window(glow_t const* l, mquad_t const* q, int* u0, int* u1, int* v0, int* v1) {
    vec3_t const d     = v3_sub(l->at, q->origin);
    float const  h     = v3_dot(d, q->n);
    float const  reach = glow_reach(l);
    if (h <= 0.0f || h >= reach) return false;
    float const  pu = v3_dot(d, q->du) / (q->su * q->su), pv = v3_dot(d, q->dv) / (q->sv * q->sv);
    float const  cu = pu < 0 ? 0 : pu > 1 ? 1 : pu, cv = pv < 0 ? 0 : pv > 1 ? 1 : pv;
    vec3_t const near = v3_add(q->origin, v3_add(v3_scale(q->du, cu), v3_scale(q->dv, cv)));
    if (v3_len(v3_sub(l->at, near)) >= reach) return false;
    // A light from behind a portal: only if part of the quad is in front of it.
    if (l->bound) {
        bool front = false;
        for (int k = 0; k < 4 && !front; k++)
            front = glow_front(l, v3_add(q->origin, v3_add(v3_scale(q->du, (float)(k & 1)),
                                                            v3_scale(q->dv, (float)(k >> 1)))));
        if (!front) return false;
    }
    // The circle the light reaches on the quad's plane, at its height.
    float const w = sqrtf(reach * reach - h * h);
    *u0           = (int)floorf(pu * q->su - w);
    *u1           = (int)ceilf(pu * q->su + w);
    *v0           = (int)floorf(pv * q->sv - w);
    *v1           = (int)ceilf(pv * q->sv + w);
    return true;
}

// A quad near a light: the cells round it lit one by one, the rest of
// the quad -- up to four strips round them -- as it was. False if no
// light reaches it.
// Cells cut for light this pass, at most GLOW_CELLS: past that a quad is
// drawn whole, unlit, rather than run the engine out of triangles.
#define GLOW_CELLS 400
static int s_glow_cells;

static bool submit_glowing(mquad_t const* q, float ou, float ov, cam_t const* cam, clipset_t const* cs,
                           material_info_t const* m) {
    // Facing away from the eye: drawn whole, and then dropped.
    if (v3_dot(v3_sub(cam->pos, q->origin), q->n) <= 0.0f) return false;
    int const nu = (int)(q->su + 0.5f), nv = (int)(q->sv + 0.5f);
    int       u0 = nu, u1 = -1, v0 = nv, v1 = -1;
    for (int i = 0; i < s_glow_n; i++) {
        int a, b, c, e;
        if (!glow_window(&s_glow[i], q, &a, &b, &c, &e)) continue;
        if (a < u0) u0 = a;
        if (b > u1) u1 = b;
        if (c < v0) v0 = c;
        if (e > v1) v1 = e;
    }
    if (u0 < 0) u0 = 0;
    if (v0 < 0) v0 = 0;
    if (u1 > nu) u1 = nu;
    if (v1 > nv) v1 = nv;
    if (u0 >= u1 || v0 >= v1 || s_glow_cells + (u1 - u0) * (v1 - v0) > GLOW_CELLS) return false;
    s_glow_cells         += (u1 - u0) * (v1 - v0);
    uint32_t const flags  = m->flags | LIT;
    float const    fu = 1.0f / (float)nu, fv = 1.0f / (float)nv;
    // The strips round the lit window, unlit.
    if (v0 > 0) submit_part(q, 0, 1, 0, (float)v0 * fv, ou, ov, cam, cs, m, flags);
    if (v1 < nv) submit_part(q, 0, 1, (float)v1 * fv, 1, ou, ov, cam, cs, m, flags);
    if (u0 > 0) submit_part(q, 0, (float)u0 * fu, (float)v0 * fv, (float)v1 * fv, ou, ov, cam, cs, m, flags);
    if (u1 < nu) submit_part(q, (float)u1 * fu, 1, (float)v0 * fv, (float)v1 * fv, ou, ov, cam, cs, m, flags);
    // The window, a cell at a time -- a run of cells with one glow along
    // a row as one quad.
    for (int b = v0; b < v1; b++) {
        int      a0 = u0;
        uint32_t g0 = 0;
        for (int a = u0; a <= u1; a++) {
            uint32_t g = 0;
            if (a < u1) {
                vec3_t const c = v3_add(q->origin, v3_add(v3_scale(q->du, ((float)a + 0.5f) * fu),
                                                          v3_scale(q->dv, ((float)b + 0.5f) * fv)));
                g              = glow_at(c, q->n);
            }
            if (a > u0 && (a == u1 || g != g0)) {
                submit_part(q, (float)a0 * fu, (float)a * fu, (float)b * fv, (float)(b + 1) * fv, ou, ov, cam, cs, m,
                            flags | SE_TRI_GLOW(g0));
                a0 = a;
            }
            g0 = g;
        }
    }
    return true;
}

static void submit_level(cam_t const* cam, clipset_t const* cs) {
    for (int i = 0; i < s_nquads; i++) {
        mquad_t const*         q    = &s_quads[i];
        material_info_t const* m    = &s_mat[q->mat];
        bool const             goo  = q->mat == MAT_GOO;
        float const            ou   = goo ? fmodf(s_time * GOO_DRIFT_U, 1.0f) : 0.0f;
        float const            ov   = goo ? fmodf(s_time * GOO_DRIFT_V, 1.0f) : 0.0f;
        // Lit by what glows: an opaque surface that is not itself a light.
        if (s_glow_n > 0 && !(m->flags & (SE_TRI_EMISSIVE | SE_TRI_BLEND)) && submit_glowing(q, ou, ov, cam, cs, m))
            continue;
        cvert_t const          v[4] = {
            {q->origin, ou, ov},
            {v3_add(q->origin, q->du), q->su + ou, ov},
            {v3_add(v3_add(q->origin, q->du), q->dv), q->su + ou, q->sv + ov},
            {v3_add(q->origin, q->dv), ou, q->sv + ov},
        };
        submit_quad(v, q->n, cam, cs, m, m->argb, m->flags | LIT);
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
        // Lit as the wall cell it stands in: the cells round it are (glow_at).
        uint32_t const g = s_glow_n > 0 ? glow_at(v3_mad(p->center, p->up, su * 0.5f * PORTAL_HALF_H), p->n) : 0;
        for (int i = 0; i < q; i++)
            portal_tri(p, corner, o[k * q + i], o[(k * q + i + 1) % PORTAL_OVAL_N], 0.0f, cam, cs, m, m->argb,
                       m->flags | SE_TRI_GLOW(g));
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

// A piece of laser beam: a thin glowing strip from a to b, turned to face
// the eye.
static void beam_face(vec3_t a, vec3_t b, float half, uint32_t argb, uint32_t flags, cam_t const* cam,
                      clipset_t const* cs) {
    vec3_t const along = v3_sub(b, a);
    vec3_t const to    = v3_sub(cam->pos, v3_scale(v3_add(a, b), 0.5f));
    vec3_t       side  = v3_cross(along, to);
    float const  len   = v3_len(side);
    if (v3_len(along) < 1e-3f || len < 1e-6f) return;
    side               = v3_scale(side, 1.0f / len);
    // At least a pixel wide at each end: narrower, a far beam fell between
    // the pixel centres and was gone.
    float const   ha   = fmaxf(half, v3_dot(v3_sub(a, cam->pos), cam->b.fwd) / RENDER_FOCAL_LEN);
    float const   hb   = fmaxf(half, v3_dot(v3_sub(b, cam->pos), cam->b.fwd) / RENDER_FOCAL_LEN);
    cvert_t const q[4] = {{v3_mad(a, side, -ha), 0, 0},
                          {v3_mad(b, side, -hb), 1, 0},
                          {v3_mad(b, side, hb), 1, 1},
                          {v3_mad(a, side, ha), 0, 1}};
    // Culled by its own normal, turned to the eye: culled by `to`, the
    // whole beam went with the eye anywhere in the ball on its first half --
    // beside it, near the emitter. One side will do: the engine culls nothing.
    vec3_t        n    = v3_cross(along, side);
    if (v3_dot(to, n) < 0.0f) n = v3_scale(n, -1.0f);
    submit_quad(q, n, cam, cs, NULL, argb, SE_TRI_EMISSIVE | flags);
}

// The same, for a copy (s_via) carried through the portals first: turned
// to the eye where it is drawn, not where the thing it is a copy of is --
// turned there, it was edge on or culled from half the room.
static void beam_strip(vec3_t a, vec3_t b, float half, uint32_t argb, uint32_t flags, cam_t const* cam,
                       clipset_t const* cs) {
    portal_t const* const via[2] = {s_via[0], s_via[1]};
    if (via[0] != NULL) {
        a        = portal_map_point(via[0], via[1], a);
        b        = portal_map_point(via[0], via[1], b);
        s_via[0] = s_via[1] = NULL;
    }
    beam_face(a, b, half, argb, flags, cam, cs);
    s_via[0] = via[0];
    s_via[1] = via[1];
}

// A rectangle c +- a +- b, turned to face the eye whichever side it is on.
static void card(vec3_t c, vec3_t a, vec3_t b, uint32_t argb, uint32_t flags, cam_t const* cam, clipset_t const* cs) {
    vec3_t n = v3_cross(a, b);
    if (v3_dot(v3_sub(cam->pos, c), n) < 0.0f) n = v3_scale(n, -1.0f);
    cvert_t const q[4] = {{v3_sub(v3_sub(c, a), b), 0, 0},
                          {v3_sub(v3_add(c, a), b), 1, 0},
                          {v3_add(v3_add(c, a), b), 1, 1},
                          {v3_add(v3_sub(c, a), b), 0, 1}};
    submit_quad(q, n, cam, cs, NULL, argb, flags);
}

// A small glowing square on a face: centre c, facing along axis direction d.
static void lens(vec3_t c, vec3_t d, float half, uint32_t argb, cam_t const* cam, clipset_t const* cs) {
    vec3_t const h =
        v3(fabsf(d.x) > 0.5f ? 0.005f : half, fabsf(d.y) > 0.5f ? 0.005f : half, fabsf(d.z) > 0.5f ? 0.005f : half);
    submit_box(v3_sub(c, h), v3_add(c, h), cam, cs, NULL, argb, SE_TRI_EMISSIVE | DECAL(1));  // a cm off its face
}

// A cube with its edges and corners bevelled off, `k` deep: six faces
// (textured, when `m` has a texture), twelve edge strips and eight corner
// triangles in `bevel`.
static void submit_cube(aabb_t const* b, float k, cam_t const* cam, clipset_t const* cs, material_info_t const* m,
                        uint32_t argb, uint32_t bevel) {
    vec3_t const c = v3_scale(v3_add(b->lo, b->hi), 0.5f);
    float const  h = (b->hi.x - b->lo.x) * 0.5f, in = h - k;
    // A point of the cube from its centre: the three offsets.
#define P(x, y, z) (cvert_t){v3_add(c, v3(x, y, z)), 0, 0}
    for (int a = 0; a < 3; a++)
        for (int s = -1; s <= 1; s += 2) {
            int const   ua = (a + 1) % 3, va = (a + 2) % 3;
            float       o[4][3];
            float const us[4] = {-in, in, in, -in}, vs[4] = {-in, -in, in, in};
            cvert_t     q[4];
            for (int i = 0; i < 4; i++) {
                o[i][a]  = (float)s * h;
                o[i][ua] = us[i];
                o[i][va] = vs[i];
                q[i]     = (cvert_t){v3_add(c, v3(o[i][0], o[i][1], o[i][2])), i == 1 || i == 2 ? 1.0f : 0.0f,
                                     i >= 2 ? 1.0f : 0.0f};
            }
            float nn[3] = {0};
            nn[a]       = (float)s;
            submit_poly(q, 4, v3(nn[0], nn[1], nn[2]), cam, cs, m, argb, 0);
        }
    // The edges: along each axis w, between faces a and b at signs sa, sb.
    for (int w = 0; w < 3; w++) {
        int const a = (w + 1) % 3, bb = (w + 2) % 3;
        for (int sa = -1; sa <= 1; sa += 2)
            for (int sb = -1; sb <= 1; sb += 2) {
                float       o[4][3];
                cvert_t     q[4];
                float const pa[4] = {(float)sa * h, (float)sa * h, (float)sa * in, (float)sa * in};
                float const pb[4] = {(float)sb * in, (float)sb * in, (float)sb * h, (float)sb * h};
                float const pw[4] = {-in, in, in, -in};
                for (int i = 0; i < 4; i++) {
                    o[i][a]  = pa[i];
                    o[i][bb] = pb[i];
                    o[i][w]  = pw[i];
                    q[i]     = (cvert_t){v3_add(c, v3(o[i][0], o[i][1], o[i][2])), 0, 0};
                }
                float nn[3] = {0};
                nn[a]       = (float)sa;
                nn[bb]      = (float)sb;
                submit_poly(q, 4, v3(nn[0], nn[1], nn[2]), cam, cs, NULL, bevel, 0);
            }
    }
    // The corners.
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                float const   x = (float)sx, y = (float)sy, z = (float)sz;
                cvert_t const q[3] = {P(x * h, y * in, z * in), P(x * in, y * h, z * in), P(x * in, y * in, z * h)};
                submit_poly(q, 3, v3(x, y, z), cam, cs, NULL, bevel, 0);
            }
#undef P
}

// A ball round c with half axes u, v and w (w through its poles), in
// `argb`, with a band round its middle: every other piece of it `light`,
// glowing if `lit`, the rest `seam`.
static void submit_ball(vec3_t c, vec3_t u, vec3_t v, vec3_t w, uint32_t argb, uint32_t light, uint32_t seam, bool lit,
                        cam_t const* cam, clipset_t const* cs) {
    enum {
        LAT = 8,
        LON = 12
    };
    vec3_t p[LAT + 1][LON];
    for (int i = 0; i <= LAT; i++)
        for (int j = 0; j < LON; j++) {
            float const a = ((float)i / LAT - 0.5f) * 3.14159265f, b = (float)j * (6.2831853f / LON);
            p[i][j] =
                v3_add(v3_add(v3_scale(u, cosf(a) * cosf(b)), v3_scale(v, cosf(a) * sinf(b))), v3_scale(w, sinf(a)));
        }
    for (int i = 0; i < LAT; i++)
        for (int j = 0; j < LON; j++) {
            int const    k   = (j + 1) % LON;
            vec3_t const out = v3_add(v3_add(p[i][j], p[i][k]), v3_add(p[i + 1][j], p[i + 1][k]));
            // The face's normal, from its diagonals, turned outwards.
            vec3_t       n   = v3_norm(v3_cross(v3_sub(p[i + 1][k], p[i][j]), v3_sub(p[i][k], p[i + 1][j])));
            if (v3_dot(n, out) < 0.0f) n = v3_scale(n, -1.0f);
            bool const     eq    = i == LAT / 2 - 1 || i == LAT / 2;  // the two rings round the middle
            uint32_t const col   = eq ? (j % 2 ? seam : light) : argb;
            uint32_t const flags = eq && j % 2 == 0 && lit ? SE_TRI_EMISSIVE : 0;
            cvert_t        q[4];
            int            nq = 0;
            q[nq++]           = (cvert_t){v3_add(c, p[i][j]), 0, 0};
            if (i > 0) q[nq++] = (cvert_t){v3_add(c, p[i][k]), 0, 0};
            q[nq++] = (cvert_t){v3_add(c, p[i + 1][k]), 0, 0};
            if (i < LAT - 1) q[nq++] = (cvert_t){v3_add(c, p[i + 1][j]), 0, 0};
            submit_poly(q, nq, n, cam, cs, NULL, col, flags);
        }
}

// A sphere of radius r, in its own axes u, v: white, with a band of lights
// round its equator that rolls with it.
static void submit_sphere(vec3_t c, float r, vec3_t u, vec3_t v, bool lit, cam_t const* cam, clipset_t const* cs) {
    submit_ball(c, v3_scale(u, r), v3_scale(v, r), v3_scale(v3_cross(u, v), r), 0xFFC8CCD2u,
                lit ? 0xFF6CE0FFu : 0xFF2C6C8Cu, 0xFF3A4048u, lit, cam, cs);
}

// A flat octagon of radius r in the plane through c facing n, spanned by
// u and v: as near round as a lens needs to be.
static void octagon(vec3_t c, vec3_t n, vec3_t u, vec3_t v, float r, uint32_t argb, uint32_t flags, cam_t const* cam,
                    clipset_t const* cs) {
    cvert_t q[8];
    for (int i = 0; i < 8; i++) {
        float const a = (float)i * 0.78539816f + 0.39269908f;
        q[i]          = (cvert_t){v3_add(c, v3_add(v3_scale(u, r * cosf(a)), v3_scale(v, r * sinf(a)))), 0, 0};
    }
    submit_poly(q, 8, n, cam, cs, NULL, argb, flags);
}

// A pedestal button: a foot, a slim post, a wide head, and on it a round
// red button in a ring of eight blue lights -- one per eighth of the time,
// going out one by one as it runs.
static void submit_pedestal(button_t const* bt, float timer, cam_t const* cam, clipset_t const* cs) {
    float const  x = (float)bt->x + 0.5f, y = (float)bt->y, z = (float)bt->z + 0.5f, top = y + 1.0f;
    vec3_t const up = v3(0, 1, 0), ux = v3(1, 0, 0), uz = v3(0, 0, 1);
    submit_box(v3(x - 0.3f, y, z - 0.3f), v3(x + 0.3f, y + 0.06f, z + 0.3f), cam, cs, NULL, 0xFF5C6066u, 0);
    submit_box(v3(x - 0.1f, y, z - 0.1f), v3(x + 0.1f, top - 0.12f, z + 0.1f), cam, cs, NULL, 0xFF8A8E96u, 0);
    submit_box(v3(x - 0.25f, top - 0.12f, z - 0.25f), v3(x + 0.25f, top, z + 0.25f), cam, cs, NULL, 0xFFB4B8BEu, 0);
    // The timer's ring, on the head round the button.
    int const lit = bt->timer_left > 0.0f && timer > 0.0f ? (int)ceilf(8.0f * bt->timer_left / timer) : 0;
    for (int k = 0; k < 8; k++) {
        float const   a0 = (float)k * 0.78539816f + 0.05f, a1 = (float)(k + 1) * 0.78539816f - 0.05f;
        float const   r0 = 0.17f, r1 = 0.23f, h = top + 0.004f;
        cvert_t const q[4] = {
            {v3(x + r0 * cosf(a0), h, z + r0 * sinf(a0)), 0, 0},
            {v3(x + r1 * cosf(a0), h, z + r1 * sinf(a0)), 0, 0},
            {v3(x + r1 * cosf(a1), h, z + r1 * sinf(a1)), 0, 0},
            {v3(x + r0 * cosf(a1), h, z + r0 * sinf(a1)), 0, 0},
        };
        bool const on = k < lit;
        submit_poly(q, 4, up, cam, cs, NULL, on ? 0xFF2C8CFFu : 0xFF30343Au, (on ? SE_TRI_EMISSIVE : 0) | DECAL(1));
    }
    // The button: down and glowing while it holds the door.
    float const h = bt->pressed ? 0.025f : 0.06f;
    submit_box(v3(x - 0.12f, top, z - 0.12f), v3(x + 0.12f, top + h, z + 0.12f), cam, cs, NULL,
               bt->pressed ? 0xFFC02818u : 0xFF901E10u, 0);
    octagon(v3(x, top + h + 0.002f, z), up, ux, uz, 0.15f, bt->pressed ? 0xFFFF6040u : 0xFFB02818u,
            (bt->pressed ? SE_TRI_EMISSIVE : 0) | DECAL(1), cam, cs);
}

// A turret: a white egg on three legs, a red eye, and a thin red line
// where it looks. Firing, yellow streaks at you and a flash at each side;
// knocked over, on its side and dark.
static void submit_turret(game_t const* g, int i, cam_t const* cam, clipset_t const* cs) {
    cube_t const* const t = &g->cubes[i];
    vec3_t const        p = t->body.pos;
    vec3_t const        f = v3(sinf(t->yaw), 0.0f, cosf(t->yaw)), s = v3(f.z, 0.0f, -f.x), up = v3(0, 1, 0);
    uint32_t const      sk = 0xFFE4E6EAu, seam = 0xFFA8ACB2u;
    if (t->down) {
        vec3_t const c = v3(p.x, p.y + 0.2f, p.z);
        submit_ball(c, v3_scale(up, 0.2f), v3_scale(s, 0.2f), v3_scale(f, 0.42f), sk, seam, seam, false, cam, cs);
        octagon(v3_add(c, v3_add(v3_scale(f, 0.19f), v3_scale(up, 0.18f))), up, f, s, 0.045f, 0xFF401010u, DECAL(1),
                cam, cs);
        return;
    }
    for (int k = 0; k < 3; k++) {  // one leg behind, two in front
        float const a = t->yaw + 3.14159265f + (float)(k - 1) * 2.0943951f;
        float const x = p.x + 0.17f * sinf(a), z = p.z + 0.17f * cosf(a);
        submit_box(v3(x - 0.025f, p.y, z - 0.025f), v3(x + 0.025f, p.y + 0.35f, z + 0.025f), cam, cs, NULL, 0xFF3A3E44u,
                   0);
    }
    vec3_t const c = v3(p.x, p.y + 0.56f, p.z);
    submit_ball(c, v3_scale(f, 0.2f), v3_scale(s, 0.2f), v3_scale(up, 0.42f), sk, seam, seam, false, cam, cs);
    vec3_t const eye = v3_add(v3(p.x, p.y + TURRET_EYE, p.z), v3_scale(f, 0.18f));
    octagon(eye, f, s, up, 0.045f, 0xFFFF2A1Cu, SE_TRI_EMISSIVE | DECAL(1), cam, cs);  // a few mm off the egg
    // A copy's sight is cast from where the eye is: out of the other
    // portal once the eye has gone through, else where the turret stands
    // (it ends on the portal, as any turret's does).
    bool const      out = s_via[0] != NULL && portal_local(s_via[0], eye).z < 0.0f;
    vec3_t const    se  = out ? portal_map_point(s_via[0], s_via[1], eye) : eye;
    vec3_t const    sf  = out ? portal_map_dir(s_via[0], s_via[1], f) : f;
    ray_hit_t const h   = level_raycast(&g->lv, se, sf, TURRET_RANGE);
    beam_strip(eye, v3_mad(eye, f, h.hit ? h.dist : TURRET_RANGE), 0.008f, 0xFFFF2A1Cu, 0, cam, cs);
    // A copy does not fire: the turret shoots from where it is.
    if (s_via[0] != NULL || !turret_firing(g, i)) return;
    vec3_t const at    = v3(g->pl.pos.x, g->pl.pos.y + 1.0f, g->pl.pos.z);
    bool const   flash = fmodf(g->burst_t, TURRET_BURST) < TURRET_BURST * 0.5f;
    for (int k = -1; k <= 1; k += 2) {
        vec3_t const muzzle = v3_add(c, v3_add(v3_scale(s, 0.21f * (float)k), v3_scale(f, 0.05f)));
        float const  j      = g->burst_t * 97.0f + (float)k;
        vec3_t const miss   = v3_add(v3_scale(s, 0.2f * sinf(j)), v3_scale(up, 0.15f * cosf(j * 1.3f)));
        beam_strip(muzzle, v3_add(at, miss), 0.012f, 0xFFFFE070u, 0, cam, cs);
        if (flash) octagon(muzzle, f, s, up, 0.08f, 0xFFFFF0A0u, SE_TRI_EMISSIVE, cam, cs);
    }
}

// A sphere lights up in a cup that is down.
static bool sphere_home(game_t const* g, int i) {
    for (int b = 0; b < g->lv.n_buttons; b++) {
        button_t const* bt = &g->lv.buttons[b];
        if (bt->sphere_only && bt->pressed && (int)floorf(g->cubes[i].body.pos.x) == bt->x &&
            (int)floorf(g->cubes[i].body.pos.z) == bt->z)
            return true;
    }
    return false;
}

// Cube i: a cube, a reflection cube, a sphere or a turret.
static void submit_body(game_t const* g, int i, cam_t const* cam, clipset_t const* cs) {
    aabb_t const b = cube_aabb(&g->cubes[i]);
    if (g->lv.cube_turret[i])
        submit_turret(g, i, cam, cs);
    else if (g->lv.cube_sphere[i])
        submit_sphere(body_center(&g->cubes[i].body), CUBE_HALF, g->cubes[i].spin[0], g->cubes[i].spin[1],
                      sphere_home(g, i), cam, cs);
    else if (!g->lv.cube_reflect[i])
        submit_cube(&b, 0.07f, cam, cs, &s_cube, s_cube.argb, 0xFF6E7278u);
    else {
        // A reflection cube: reddish, with a red lens on the side the beam
        // leaves by -- out along f to the surface: further on a diagonal,
        // to the edge.
        submit_cube(&b, 0.07f, cam, cs, NULL, 0xFFA48A8Au, 0xFF8A7070u);
        body_t const* p = &g->cubes[i].body;
        vec3_t const  f = v3(sinf(g->cubes[i].yaw), 0.0f, cosf(g->cubes[i].yaw));
        float const   t = CUBE_HALF / fmaxf(fabsf(f.x), fabsf(f.z)) + 0.01f;
        vec3_t const  c = v3_mad(v3(p->pos.x, p->pos.y + CUBE_HALF, p->pos.z), f, t);
        submit_box(v3(c.x - 0.08f, c.y - 0.08f, c.z - 0.08f), v3(c.x + 0.08f, c.y + 0.08f, c.z + 0.08f), cam, cs, NULL,
                   0xFFFF3A28u, SE_TRI_EMISSIVE);
    }
}

// The portal cube i is part way through -- its box across the plane,
// its middle over the opening -- or -1.
static int body_portal(game_t const* g, int i) {
    if (!g->portals[0].open || !g->portals[1].open) return -1;
    aabb_t const b = cube_aabb(&g->cubes[i]);
    vec3_t const c = v3_scale(v3_add(b.lo, b.hi), 0.5f), h = v3_scale(v3_sub(b.hi, b.lo), 0.5f);
    for (int k = 0; k < 2; k++) {
        portal_t const* p  = &g->portals[k];
        vec3_t const    l  = portal_local(p, c);
        float const     hn = fabsf(p->n.x) * h.x + fabsf(p->n.y) * h.y + fabsf(p->n.z) * h.z;
        if (fabsf(l.z) < hn && fabsf(l.x) < PORTAL_HALF_W && fabsf(l.y) < PORTAL_HALF_H) return k;
    }
    return -1;
}

static game_t const* s_game;  // the frame being drawn (render_frame)

// The lift stations (lift.h): drawn from what main.c says of them.
static lift_view_t s_lift_view;

void render_set_lift(lift_view_t const* v) {
    if (v == NULL)
        memset(&s_lift_view, 0, sizeof(s_lift_view));
    else
        s_lift_view = *v;
    s_lift_view.hide_cube = v == NULL ? -1 : v->hide_cube;
}

#define LIFT_SIDE_A (3.14159265f / 4.0f)

// A point on station `s`'s ellipse, `grow` out from it, at angle `a`.
static vec3_t lift_at(lift_site_t const* s, float a, float grow, float y) {
    return v3(s->x + (s->rx + grow) * sinf(a), y, s->z + (s->rz + grow) * cosf(a));
}

static bool lift_inside(lift_site_t const* s, vec3_t e) {
    float const dx = (e.x - s->x) / s->rx, dz = (e.z - s->z) / s->rz;
    return dx * dx + dz * dz < 1.0f;
}

// Side i of a band round the station, y0 to y1: facing the eye -- in from
// inside the station, out from outside.
static void lift_side(lift_site_t const* s, int i, float grow, float y0, float y1, bool in, cam_t const* cam,
                      clipset_t const* cs, material_info_t const* m, uint32_t argb, uint32_t flags) {
    float const  a0 = (float)i * LIFT_SIDE_A, a1 = (float)(i + 1) * LIFT_SIDE_A, am = (a0 + a1) * 0.5f;
    vec3_t const p0 = lift_at(s, a0, grow, y0), p1 = lift_at(s, a1, grow, y0);
    vec3_t       n  = v3_norm(v3(sinf(am) / (s->rx + grow), 0, cosf(am) / (s->rz + grow)));
    if (in) n = v3_scale(n, -1.0f);
    float const   h    = y1 - y0;
    cvert_t const q[4] = {{p0, 0, 0}, {p1, 1, 0}, {v3(p1.x, y1, p1.z), 1, h}, {v3(p0.x, y1, p0.z), 0, h}};
    submit_quad(q, n, cam, cs, m, argb, flags);
}

static void lift_band(lift_site_t const* s, float grow, float y0, float y1, bool in, cam_t const* cam,
                      clipset_t const* cs, uint32_t argb, uint32_t flags) {
    for (int i = 0; i < LIFT_SIDES; i++) lift_side(s, i, grow, y0, y1, in, cam, cs, NULL, argb, flags);
}

// A flat octagon at height y, facing up (or down).
static void lift_disc(lift_site_t const* s, float grow, float y, bool up, cam_t const* cam, clipset_t const* cs,
                      uint32_t argb, uint32_t flags) {
    cvert_t q[LIFT_SIDES];
    for (int i = 0; i < LIFT_SIDES; i++) {
        int const k = up ? LIFT_SIDES - 1 - i : i;
        q[i]        = (cvert_t){lift_at(s, (float)k * LIFT_SIDE_A, grow, y), 0, 0};
    }
    submit_poly(q, LIFT_SIDES, v3(0, up ? 1.0f : -1.0f, 0), cam, cs, NULL, argb, flags);
}

// An open portal on the floor (or ceiling) at height y near station s.
static bool lift_portal_near(lift_site_t const* s, float y, bool floor) {
    for (int i = 0; i < 2; i++) {
        portal_t const* p = &s_game->portals[i];
        if (!p->open || (floor ? p->n.y < 0.9f : p->n.y > -0.9f) || fabsf(p->center.y - y) > 0.2f) continue;
        if (hypotf(p->center.x - s->x, p->center.z - s->z) < fmaxf(s->rx, s->rz) + 0.9f) return true;
    }
    return false;
}

static void submit_lift_solid(cam_t const* cam, clipset_t const* cs) {
    for (int k = 0; k < s_lift_view.n; k++) {
        lift_station_view_t const* v = &s_lift_view.st[k];
        lift_site_t const*         s = &v->site;
        vec3_t const               e = cam->pos;
        bool const                 in = lift_inside(s, e);
        float const                fy = s->y, car = fy + v->dy, top = fy + (s->hatch ? s->ceil : s->mouth);
        // A car going up into a mouth hung in the air goes no higher than it.
        float const cap = !s->hatch && !v->shaft ? fy + s->mouth : 1e9f;
        // Too far off to matter, or wholly behind the eye -- never the one
        // ridden or stood in.
        if (!in && !v->shaft && v->dy <= 0.0f) {
            vec3_t const ce = v3_sub(v3(s->x, (fy + top) * 0.5f, s->z), e);
            float const  r  = fmaxf(fmaxf(s->rx, s->rz), (top - fy) * 0.5f) + 0.1f;
            if (v3_len(ce) > 40.0f + r || v3_dot(ce, cam->b.fwd) < -r) continue;
        }
        // The collar on the floor: green for the way out, white for the way in.
        if (!lift_portal_near(s, fy, true))
            lift_band(s, 0.02f, fy, fy + 0.08f, in, cam, cs, v->exit ? 0xFF6CF0C8u : 0xFFE4ECFFu,
                      SE_TRI_EMISSIVE | DECAL(1));
        // The halo, riding with the car, and its door light under it --
        // none once the car has gone up into its hatch.
        if (!v->empty && car + s->cb + 0.15f <= cap) {
            lift_band(s, 0.0f, car + s->cb, car + s->cb + 0.15f, in, cam, cs, 0xFFE8ECF0u, 0);
            lift_band(s, 0.005f, car + s->cb - 0.03f, car + s->cb, in, cam, cs,
                      v->shut_light ? 0xFFFF8A1Cu : 0xFF2C8CFFu, SE_TRI_EMISSIVE | DECAL(1));
        }
        // Four rails, floor to hatch, turned to the eye.
        for (int r = 0; r < 4; r++) {
            float const  a    = ((float)r * 2.0f + 1.0f) * LIFT_SIDE_A;
            vec3_t const p    = lift_at(s, a, 0.04f, fy);
            vec3_t const to   = v3_norm(v3(e.x - p.x, 0, e.z - p.z));
            vec3_t const side = v3(to.z * 0.025f, 0, -to.x * 0.025f);
            cvert_t const q[4] = {{v3_sub(p, side), 0, 0},
                                  {v3_add(p, side), 0, 0},
                                  {v3_add(v3(p.x, top, p.z), side), 0, 0},
                                  {v3_sub(v3(p.x, top, p.z), side), 0, 0}};
            submit_quad(q, to, cam, cs, NULL, 0xFFD8DCE0u, 0);
        }
        if (s->hatch) {
            if (!lift_portal_near(s, fy + s->ceil, false)) lift_disc(s, 0.06f, fy + s->ceil, false, cam, cs, 0xFF202428u, DECAL(1));
        } else {
            lift_band(s, 0.06f, fy + s->mouth, fy + s->mouth + 0.12f, in, cam, cs, 0xFFB8BEC6u, 0);  // the mouth, hung in the air
        }
        // The car's floor, once it is off the ground.
        if (v->dy > 0.02f && !v->empty && car < cap - 0.02f) lift_disc(s, 0.06f, car, true, cam, cs, 0xFF3A3E44u, 0);
        // Riding: the shaft, lit, from the mouth up.
        if (v->shaft && e.y > fy + s->mouth - 1.0f) {
            float const y0 = fy + s->mouth, y1 = y0 + s->rise + 2.4f;
            lift_band(s, 0.06f, y0, y1, true, cam, cs, 0xFF181A1Eu, 0);
            for (int b = 0; b < 3; b++) {
                float const y = y0 + 0.9f + 1.2f * (float)b;
                lift_band(s, 0.05f, y, y + 0.04f, true, cam, cs, 0xFFBFE8FFu, SE_TRI_EMISSIVE | DECAL(1));
            }
            lift_disc(s, 0.06f, y1, false, cam, cs, 0xFF101214u, 0);
        }
    }
}

// The glass: the panels that stay, and the doors as far down as they are.
// One layer of a convex station never overlaps itself, so in the order
// given it needs no sorting: the far station first.
static void submit_lift_glass(cam_t const* cam, clipset_t const* cs) {
    material_info_t const* m = &s_mat[MAT_GLASS];
    if (m->tex == NULL) return;  // without its texture glass would be a wall
    int order[2] = {0, 1};
    if (s_lift_view.n == 2) {
        float const d0 = v3_len(v3_sub(v3(s_lift_view.st[0].site.x, 0, s_lift_view.st[0].site.z), cam->pos));
        float const d1 = v3_len(v3_sub(v3(s_lift_view.st[1].site.x, 0, s_lift_view.st[1].site.z), cam->pos));
        if (d1 > d0) order[0] = 1, order[1] = 0;
    }
    for (int o = 0; o < s_lift_view.n; o++) {
        lift_station_view_t const* v  = &s_lift_view.st[order[o]];
        lift_site_t const*         s  = &v->site;
        if (v->empty) continue;
        bool const                 in = lift_inside(s, cam->pos);
        float const car = s->y + v->dy;
        float const top = fminf(car + s->cb - 0.03f, !s->hatch && !v->shaft ? s->y + s->mouth : 1e9f);
        for (int i = 0; i < LIFT_SIDES; i++) {
            float const shut = (s->fixed & (1u << i)) ? 1.0f : v->closed[i];
            if (shut < 0.01f) continue;
            float const y0 = top - (top - car) * shut;
            if (y0 >= top - 0.01f) continue;  // gone up into the mouth
            lift_side(s, i, 0.0f, y0, top, in, cam, cs, m, m->argb, m->flags);
            if (!(s->fixed & (1u << i)))  // the door's lit edge
                lift_side(s, i, 0.004f, y0, y0 + 0.02f, in, cam, cs, NULL, 0xFFBFE8FFu, SE_TRI_EMISSIVE);
        }
    }
}

static void submit_things(game_t const* g, cam_t const* cam, clipset_t const* cs) {
    for (int i = 0; i < g->n_cubes; i++) {
        if (g->cubes[i].gone || i == s_lift_view.hide_cube) continue;
        // Part way through a portal: drawn again, carried out of the other
        // one. Drawn once, what had gone past the plane was missing, and
        // through the portal the cut cube showed hollow, flickering as it
        // crossed. Each copy is cut at its own portal's plane: a turret's
        // sight went on through a thin wall into the room behind.
        int const k = body_portal(g, i);
        if (k >= 0) keep_front(&g->portals[k]);
        submit_body(g, i, cam, cs);
        s_keep.n = 0;
        if (k < 0) continue;
        s_via[0] = &g->portals[k];
        s_via[1] = &g->portals[k ^ 1];
        keep_front(s_via[1]);
        submit_body(g, i, cam, cs);
        s_keep.n = 0;
        s_via[0] = s_via[1] = NULL;
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
        if (bt->pedestal) {
            submit_pedestal(bt, g->lv.timer, cam, cs);
            continue;
        }
        // A relay's button sits on its slim post: a smaller plate.
        float const rim = bt->relay ? 0.25f : 0.05f;
        submit_box(v3(x + rim, top, z + rim), v3(x + 1.0f - rim, top + 0.04f, z + 1.0f - rim), cam, cs, NULL,
                   0xFF5C6066u, 0);
        if (bt->relay) {
            // The post, with three rings that glow while a beam goes through.
            float const    y0 = (float)bt->y, cx = x + 0.5f, cz = z + 0.5f;
            uint32_t const ring = bt->pressed ? 0xFFFF3A28u : 0xFF3A2020u;
            submit_box(v3(cx - 0.08f, y0, cz - 0.08f), v3(cx + 0.08f, top, cz + 0.08f), cam, cs, NULL, 0xFF8A8E96u, 0);
            for (int k = 0; k < 3; k++) {
                float const ry = y0 + 0.3f + 0.2f * (float)k;
                submit_box(v3(cx - 0.14f, ry, cz - 0.14f), v3(cx + 0.14f, ry + 0.06f, cz + 0.14f), cam, cs, NULL, ring,
                           bt->pressed ? SE_TRI_EMISSIVE : 0);
            }
        }
        float const    h   = bt->pressed ? 0.06f : 0.12f;
        // Red for anyone, blue for a cube only, cyan for a sphere.
        uint32_t const lit = bt->sphere_only ? 0xFF6CE0FFu : bt->cube_only ? 0xFF40A0FFu : 0xFFFF6040u;
        uint32_t const off = bt->sphere_only ? 0xFF2C6C8Cu : bt->cube_only ? 0xFF1C4C90u : 0xFFB02818u;
        if (bt->sphere_only) {
            // A cup: a flat ring of light the sphere sits in.
            octagon(v3(x + 0.5f, top + 0.042f, z + 0.5f), v3(0, 1, 0), v3(1, 0, 0), v3(0, 0, 1), 0.4f,
                    bt->pressed ? lit : off, (bt->pressed ? SE_TRI_EMISSIVE : 0) | DECAL(1), cam, cs);
            octagon(v3(x + 0.5f, top + 0.044f, z + 0.5f), v3(0, 1, 0), v3(1, 0, 0), v3(0, 0, 1), 0.28f, 0xFF3A4048u,
                    DECAL(2), cam, cs);
            continue;
        }
        submit_box(v3(x + 0.2f, top, z + 0.2f), v3(x + 0.8f, top + h, z + 0.8f), cam, cs, NULL, bt->pressed ? lit : off,
                   bt->pressed ? SE_TRI_EMISSIVE : 0);
    }
    // Lasers: the emitter's lens, and the beam as traced last step.
    for (int k = 0; k < g->lv.n_lasers; k++) {
        emitter_t const* L = &g->lv.lasers[k];
        vec3_t const     d = dir_vec(L->dir);
        lens(v3_mad(v3((float)L->x + 0.5f, (float)L->y + 0.5f, (float)L->z + 0.5f), d, 0.505f), d, 0.25f, 0xFFFF3A28u,
             cam, cs);
        for (int i = 0; i < g->beam_n[k]; i++)
            beam_strip(g->beam[k][i].a, g->beam[k][i].b, 0.025f, 0xFFFF3A28u, 0, cam, cs);
    }
    // Light bridges: a pale blue slab, with glowing edges, along each piece.
    for (int k = 0; k < g->lv.n_bridges; k++) {
        emitter_t const* E = &g->lv.bridges[k];
        vec3_t const     d = dir_vec(E->dir);
        lens(v3_mad(v3((float)E->x + 0.5f, (float)E->y + 0.15f, (float)E->z + 0.5f), d, 0.505f), d, 0.1f, 0xFF8CD8FFu,
             cam, cs);
        for (int i = 0; i < g->bridge_n[k]; i++) {
            beam_seg_t const* s    = &g->bridge[k][i];
            bool const        on_x = fabsf(s->b.x - s->a.x) > fabsf(s->b.z - s->a.z);
            // Drawn a hair above its surface: where it lies on a floor, the
            // floor would show through.
            vec3_t const      lo   = v3(fminf(s->a.x, s->b.x), s->a.y - 0.05f, fminf(s->a.z, s->b.z));
            vec3_t const      hi   = v3(fmaxf(s->a.x, s->b.x), s->a.y + 0.012f, fmaxf(s->a.z, s->b.z));
            vec3_t const      w    = on_x ? v3(0, 0, 0.5f) : v3(0.5f, 0, 0);
            vec3_t const      e    = on_x ? v3(0, 0, 0.04f) : v3(0.04f, 0, 0);  // its two edges, lit
            vec3_t const      up   = v3(0, 0.01f, 0);
            submit_box(v3_sub(lo, w), v3_add(hi, w), cam, cs, NULL, 0xFF78C0E8u, 0);
            // The edges share the slab's side and bottom: decals.
            submit_box(v3_sub(lo, w), v3_add(v3_add(v3_sub(hi, w), e), up), cam, cs, NULL, 0xFFB8ECFFu,
                       SE_TRI_EMISSIVE | DECAL(1));
            submit_box(v3_sub(v3_add(lo, w), e), v3_add(v3_add(hi, w), up), cam, cs, NULL, 0xFFB8ECFFu,
                       SE_TRI_EMISSIVE | DECAL(1));
        }
    }
    // Excursion funnels: the edges of a tube of light, blue -- orange while
    // turned round -- with rings sliding along it the way it carries. (No
    // see-through walls: the engine blends textured triangles only.)
    {
        bool const     rev  = funnel_reversed(g);
        uint32_t const wall = rev ? 0xFFFF9A40u : 0xFF4A9AFFu, ring = rev ? 0xFFFFC080u : 0xFF9CD4FFu;
        float          ph = fmodf(s_time * (rev ? -FUNNEL_SPEED : FUNNEL_SPEED), 1.0f);
        if (ph < 0.0f) ph += 1.0f;
        for (int k = 0; k < g->lv.n_funnels; k++) {
            emitter_t const* E = &g->lv.funnels[k];
            vec3_t const     d = dir_vec(E->dir);
            lens(v3_mad(v3((float)E->x + 0.5f, (float)E->y + 0.5f, (float)E->z + 0.5f), d, 0.505f), d, 0.3f, ring, cam,
                 cs);
            float run = 0.0f;  // along the funnel, from its emitter
            for (int i = 0; i < g->funnel_n[k]; i++) {
                beam_seg_t const* s   = &g->funnel[k][i];
                vec3_t const      ab  = v3_sub(s->b, s->a);
                float const       len = v3_len(ab);
                if (len < 1e-3f) continue;
                vec3_t const a        = v3_scale(ab, 1.0f / len);
                vec3_t const u        = fabsf(a.y) > 0.5f ? v3(1, 0, 0) : v3(0, 1, 0);
                vec3_t const v        = fabsf(a.z) > 0.5f ? v3(1, 0, 0) : v3(0, 0, 1);
                vec3_t const side[4]  = {u, v3_scale(u, -1.0f), v, v3_scale(v, -1.0f)};
                vec3_t const other[4] = {v, v, u, u};
                for (int w = 0; w < 4; w++) {  // its four edges
                    vec3_t const e = v3_add(v3_scale(u, w & 1 ? 0.48f : -0.48f), v3_scale(v, w & 2 ? 0.48f : -0.48f));
                    beam_strip(v3_add(s->a, e), v3_add(s->b, e), 0.02f, wall, DECAL(1), cam,
                               cs);  // along a wall or floor
                }
                for (float t = ceilf(run - ph) + ph; t < run + len; t += 1.0f) {
                    if (t < run) continue;
                    vec3_t const p = v3_mad(s->a, a, t - run);
                    for (int w = 0; w < 4; w++)
                        card(v3_mad(p, side[w], 0.49f), v3_scale(a, 0.04f), v3_scale(other[w], 0.49f), ring,
                             SE_TRI_EMISSIVE | DECAL(1), cam, cs);
                }
                run += len;
            }
        }
    }
    // Crushers: a metal block with a band of warning light round its foot.
    for (int k = 0; k < g->lv.n_crushers; k++) {
        aabb_t const b = crusher_aabb(g, k);
        submit_box(b.lo, b.hi, cam, cs, &s_mat[MAT_METAL], s_mat[MAT_METAL].argb, 0);
        submit_box(v3(b.lo.x - 0.01f, b.lo.y, b.lo.z - 0.01f), v3(b.hi.x + 0.01f, b.lo.y + 0.08f, b.hi.z + 0.01f), cam,
                   cs, NULL, 0xFFFF8A1Cu, SE_TRI_EMISSIVE);
    }
    // Gel in flight.
    static uint32_t const gel_argb[] = {0, 0xFF2E7BFFu, 0xFFFF8A1Cu, 0xFFF0F0EAu};
    for (int i = 0; i < GEL_BLOBS; i++) {
        gel_blob_t const* b = &g->blobs[i];
        if (!b->live) continue;
        vec3_t const h = v3(0.07f, 0.07f, 0.07f);
        submit_box(v3_sub(b->pos, h), v3_add(b->pos, h), cam, cs, NULL, gel_argb[b->gel], SE_TRI_EMISSIVE);
    }
    // Energy pellets: a white-hot ball in an orange glow, turned to the eye.
    for (int k = 0; k < g->lv.n_launchers; k++) {
        pellet_t const* p = &g->pellets[k];
        if (!p->live) continue;
        vec3_t const to = v3_norm(v3_sub(cam->pos, p->pos));
        vec3_t       u  = v3_cross(to, v3(0, 1, 0));
        if (v3_len(u) < 1e-3f) u = v3(1, 0, 0);
        u              = v3_norm(u);
        vec3_t const v = v3_cross(u, to);
        octagon(p->pos, to, u, v, 0.22f, 0xFFFF8A1Cu, SE_TRI_EMISSIVE, cam, cs);
        octagon(v3_mad(p->pos, to, 0.01f), to, u, v, 0.12f, 0xFFFFF4C8u, SE_TRI_EMISSIVE | DECAL(1), cam, cs);
    }
    // A launcher's mouth: a dark ring round an orange glow.
    for (int k = 0; k < g->lv.n_launchers; k++) {
        emitter_t const* L = &g->lv.launchers[k];
        vec3_t const     n = dir_vec(L->dir);
        vec3_t const     u = fabsf(n.y) > 0.5f ? v3(1, 0, 0) : v3(fabsf(n.z), 0, fabsf(n.x));
        vec3_t const     v = fabsf(n.y) > 0.5f ? v3(0, 0, 1) : v3(0, 1, 0);
        vec3_t const     c = v3_mad(v3((float)L->x + 0.5f, (float)L->y + 0.5f, (float)L->z + 0.5f), n, 0.503f);
        octagon(c, n, u, v, 0.40f, 0xFF2A2A2Eu, DECAL(1), cam, cs);
        octagon(v3_mad(c, n, 0.003f), n, u, v, 0.18f, g->pellets[k].done ? 0xFF4A3018u : 0xFFFF8A1Cu,
                (g->pellets[k].done ? 0 : SE_TRI_EMISSIVE) | DECAL(2), cam, cs);
    }
    // Laser catchers and pellet receivers: a lens on each open side, in a
    // metal ring, dark until a beam or a pellet lights it.
    for (int i = 0; i < g->lv.n_buttons; i++) {
        button_t const* bt = &g->lv.buttons[i];
        if (!bt->laser && !bt->receiver) continue;
        vec3_t const mid = v3((float)bt->x + 0.5f, (float)bt->y + 0.5f, (float)bt->z + 0.5f);
        for (int face = 0; face < 6; face++) {
            if (face == DIR_PY || face == DIR_NY) continue;  // the button is on top
            int dx, dy, dz;
            dir_step(face, &dx, &dy, &dz);
            if (level_solid(&g->lv, bt->x + dx, bt->y + dy, bt->z + dz)) continue;
            vec3_t const n = dir_vec(face);
            vec3_t const u = v3(fabsf(n.z), 0, fabsf(n.x)), v = v3(0, 1, 0);
            octagon(v3_mad(mid, n, 0.503f), n, u, v, 0.40f, 0xFF8A8E96u, DECAL(1), cam, cs);
            uint32_t const glow = bt->receiver ? 0xFFFF8A1Cu : 0xFFFF3A28u;
            octagon(v3_mad(mid, n, 0.506f), n, u, v, 0.30f, bt->pressed ? glow : 0xFF1A1414u,
                    (bt->pressed ? SE_TRI_EMISSIVE : 0) | DECAL(2), cam, cs);
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
        submit_box(llo, lhi, cam, cs, NULL, light, SE_TRI_EMISSIVE | DECAL(1));  // 1 cm proud of the panels
    }
}

// --- The player -----------------------------------------------------------

// Chell, the test subject: never drawn in the eye's own pass (the camera
// is in her head), only in the views through the portals, so you can see
// yourself. Chunky like the turrets and cubes: boxes and faceted balls in
// flat colours. Body turned by yaw, the gun pointing by yaw and pitch.

static bool s_chell_pass;  // draw_pass: a view through a portal

// A box round c with half axes a, b, w (at right angles): the faces that
// face the eye.
static void obox(vec3_t c, vec3_t a, vec3_t b, vec3_t w, uint32_t argb, uint32_t flags, cam_t const* cam,
                 clipset_t const* cs) {
    vec3_t const ax[3] = {a, b, w};
    for (int k = 0; k < 3; k++)
        for (int s = -1; s <= 1; s += 2) {
            vec3_t const  n  = v3_scale(ax[k], (float)s);
            vec3_t const  fc = v3_add(c, n);
            vec3_t const  u = ax[(k + 1) % 3], v = ax[(k + 2) % 3];
            cvert_t const q[4] = {{v3_sub(v3_sub(fc, u), v), 0, 0},
                                  {v3_sub(v3_add(fc, u), v), 0, 0},
                                  {v3_add(v3_add(fc, u), v), 0, 0},
                                  {v3_add(v3_sub(fc, u), v), 0, 0}};
            submit_poly(q, 4, n, cam, cs, NULL, argb, flags);
        }
}

// A limb: a box from a to b, `w` across (along cross(b - a, hint)) and
// `h` the other way, both half widths.
static void limb(vec3_t a, vec3_t b, float w, float h, vec3_t hint, uint32_t argb, cam_t const* cam,
                 clipset_t const* cs) {
    vec3_t const ax = v3_scale(v3_sub(b, a), 0.5f);
    vec3_t       s  = v3_cross(ax, hint);
    if (v3_len(s) < 1e-5f) s = v3_cross(ax, v3(1, 0, 0));
    s              = v3_norm(s);
    vec3_t const t = v3_norm(v3_cross(s, ax));
    obox(v3_add(a, ax), ax, v3_scale(s, w), v3_scale(t, h), argb, 0, cam, cs);
}

// A faceted ball round c, half axes u, v and w (w through its poles):
// `lat` rings of `lon` faces.
static void facet_ball(vec3_t c, vec3_t u, vec3_t v, vec3_t w, int lat, int lon, uint32_t argb, cam_t const* cam,
                       clipset_t const* cs) {
    vec3_t p[9][12];
    for (int i = 0; i <= lat; i++)
        for (int j = 0; j < lon; j++) {
            float const a = ((float)i / (float)lat - 0.5f) * 3.14159265f,
                        b = ((float)j + 0.5f) * (6.2831853f / (float)lon);
            p[i][j] =
                v3_add(v3_add(v3_scale(u, cosf(a) * cosf(b)), v3_scale(v, cosf(a) * sinf(b))), v3_scale(w, sinf(a)));
        }
    for (int i = 0; i < lat; i++)
        for (int j = 0; j < lon; j++) {
            int const    k   = (j + 1) % lon;
            vec3_t const out = v3_add(v3_add(p[i][j], p[i][k]), v3_add(p[i + 1][j], p[i + 1][k]));
            vec3_t       n   = v3_norm(v3_cross(v3_sub(p[i + 1][k], p[i][j]), v3_sub(p[i][k], p[i + 1][j])));
            if (v3_dot(n, out) < 0.0f) n = v3_scale(n, -1.0f);
            cvert_t q[4];
            int     nq = 0;
            q[nq++]    = (cvert_t){v3_add(c, p[i][j]), 0, 0};
            if (i > 0) q[nq++] = (cvert_t){v3_add(c, p[i][k]), 0, 0};
            q[nq++] = (cvert_t){v3_add(c, p[i + 1][k]), 0, 0};
            if (i < lat - 1) q[nq++] = (cvert_t){v3_add(c, p[i + 1][j]), 0, 0};
            submit_poly(q, nq, n, cam, cs, NULL, argb, 0);
        }
}

#define CH_ORANGE 0xFFE8701Cu  // the jumpsuit
#define CH_TIED   0xFFC0561Au  // its top, tied round the waist
#define CH_TANK   0xFFECECE6u
#define CH_SKIN   0xFFD9A07Au
#define CH_HAIR   0xFF2E1E14u
#define CH_BOOT   0xFF5E636Bu
#define CH_SOLE   0xFF16181Cu
#define CH_SPRING 0xFFB8BCC4u
#define CH_GUN    0xFFEEF0F2u
#define CH_GUN_DK 0xFF1C1E22u

static void submit_chell_at(player_t const* pl, cam_t const* cam, clipset_t const* cs) {
    vec3_t const  o  = pl->pos;
    float const   sy = sinf(pl->yaw), cy = cosf(pl->yaw);
    vec3_t const  f = v3(sy, 0, cy), r = v3(cy, 0, -sy), up = v3(0, 1, 0);
    basis_t const view = player_view(pl);
    vec3_t const  d = view.fwd, gu = view.up, gr = view.right;
    // A point of her, from her feet: x right, y up, z forward.
#define P(x, y, z) v3_add(o, v3_add(v3_scale(r, (x)), v3_add(v3_scale(up, (y)), v3_scale(f, (z)))))
#define BOX(x, y, z, hx, hy, hz, col) \
    obox(P(x, y, z), v3_scale(r, hx), v3_scale(up, hy), v3_scale(f, hz), col, 0, cam, cs)
    for (int s = -1; s <= 1; s += 2) {
        float const x = 0.085f * (float)s;
        // Long fall boots: a black sole under the front of the foot, the
        // grey boot up the shin, and the spring blade down the back of
        // the calf to the heel.
        BOX(x, 0.035f, 0.07f, 0.055f, 0.03f, 0.12f, CH_SOLE);
        BOX(x, 0.20f, 0.0f, 0.062f, 0.15f, 0.07f, CH_BOOT);
        BOX(x, 0.17f, -0.10f, 0.025f, 0.16f, 0.018f, CH_SPRING);
        // The leg.
        BOX(x, 0.61f, 0.0f, 0.066f, 0.26f, 0.075f, CH_ORANGE);
    }
    BOX(0.0f, 0.93f, 0.0f, 0.165f, 0.08f, 0.092f, CH_ORANGE);  // hips
    BOX(0.0f, 0.985f, 0.0f, 0.178f, 0.04f, 0.104f, CH_TIED);   // the sleeves round the waist
    BOX(0.0f, 0.95f, 0.115f, 0.045f, 0.04f, 0.02f, CH_TIED);   // the knot
    BOX(-0.045f, 0.84f, 0.105f, 0.028f, 0.08f, 0.015f, CH_TIED);
    BOX(0.05f, 0.85f, 0.105f, 0.028f, 0.07f, 0.015f, CH_TIED);
    BOX(0.0f, 0.86f, -0.108f, 0.15f, 0.10f, 0.016f, CH_TIED);  // the top's back, hanging
    BOX(0.0f, 1.13f, 0.0f, 0.135f, 0.12f, 0.08f, CH_TANK);     // tank top
    BOX(0.0f, 1.31f, 0.0f, 0.165f, 0.08f, 0.09f, CH_TANK);
    BOX(0.0f, 1.43f, 0.0f, 0.04f, 0.05f, 0.04f, CH_SKIN);  // neck
    // Head and hair: the hair a size bigger and further back, so the face
    // shows in front.
    facet_ball(P(0.0f, 1.565f, 0.01f), v3_scale(r, 0.092f), v3_scale(f, 0.105f), v3_scale(up, 0.115f), 4, 8, CH_SKIN,
               cam, cs);
    facet_ball(P(0.0f, 1.59f, -0.025f), v3_scale(r, 0.104f), v3_scale(f, 0.112f), v3_scale(up, 0.122f), 4, 8, CH_HAIR,
               cam, cs);
    limb(P(0.0f, 1.64f, -0.11f), P(0.0f, 1.40f, -0.19f), 0.04f, 0.035f, r, CH_HAIR, cam, cs);  // ponytail
    // The portal gun, held at her right side, pointing where she looks.
    vec3_t const g = P(0.12f, 1.10f, 0.20f);
#define G(x, y, z) v3_add(g, v3_add(v3_scale(gr, (x)), v3_add(v3_scale(gu, (y)), v3_scale(d, (z)))))
#define GBOX(x, y, z, hx, hy, hz, col, fl) \
    obox(G(x, y, z), v3_scale(gr, hx), v3_scale(gu, hy), v3_scale(d, hz), col, fl, cam, cs)
    GBOX(0.0f, 0.0f, -0.08f, 0.072f, 0.075f, 0.19f, CH_GUN, 0);                           // the white body
    GBOX(0.0f, -0.10f, -0.06f, 0.026f, 0.05f, 0.035f, CH_GUN_DK, 0);                      // the grip
    GBOX(0.0f, 0.0f, 0.14f, 0.058f, 0.058f, 0.035f, CH_GUN_DK, 0);                        // the black collar
    GBOX(0.0f, 0.0f, 0.24f, 0.026f, 0.026f, 0.07f, s_rim[s_last_shot], SE_TRI_EMISSIVE);  // the glowing tip
    GBOX(0.0f, 0.054f, 0.26f, 0.014f, 0.016f, 0.10f, CH_GUN_DK, 0);                       // its three claws
    GBOX(0.05f, -0.03f, 0.26f, 0.016f, 0.014f, 0.10f, CH_GUN_DK, 0);
    GBOX(-0.05f, -0.03f, 0.26f, 0.016f, 0.014f, 0.10f, CH_GUN_DK, 0);
    // Bare arms: the right hand on the grip, the left under the front.
    vec3_t const hand[2] = {G(-0.06f, -0.07f, 0.06f), G(0.0f, -0.12f, -0.06f)};
    for (int s = 0; s < 2; s++) {
        float const  side     = s ? 1.0f : -1.0f;
        vec3_t const shoulder = P(0.195f * side, 1.35f, 0.0f);
        vec3_t const elbow =
            v3_add(v3_scale(v3_add(shoulder, hand[s]), 0.5f), v3_add(v3_scale(r, 0.06f * side), v3_scale(up, -0.09f)));
        limb(shoulder, elbow, 0.042f, 0.042f, f, CH_SKIN, cam, cs);
        limb(elbow, hand[s], 0.036f, 0.036f, up, CH_SKIN, cam, cs);
    }
#undef GBOX
#undef G
#undef BOX
#undef P
}

// Her box across a portal's plane, over the opening: the portal she is
// part way through, or -1. As body_portal.
static int chell_portal(game_t const* g, player_t const* pl) {
    if (!g->portals[0].open || !g->portals[1].open) return -1;
    vec3_t const c = v3(pl->pos.x, pl->pos.y + PL_HEIGHT * 0.5f, pl->pos.z);
    vec3_t const h = v3(PL_HALF_W, PL_HEIGHT * 0.5f, PL_HALF_W);
    for (int k = 0; k < 2; k++) {
        portal_t const* p  = &g->portals[k];
        vec3_t const    l  = portal_local(p, c);
        float const     hn = fabsf(p->n.x) * h.x + fabsf(p->n.y) * h.y + fabsf(p->n.z) * h.z;
        if (fabsf(l.z) < hn && fabsf(l.x) < PORTAL_HALF_W && fabsf(l.y) < PORTAL_HALF_H) return k;
    }
    return -1;
}

// Not a copy with the eye in her head: stepping through a portal, the
// view through it is seen from where her copy out of the other one is.
static bool chell_eye_inside(player_t const* pl, cam_t const* cam) {
    vec3_t e = player_eye(pl);
    if (s_via[0] != NULL) e = portal_map_point(s_via[0], s_via[1], e);
    return v3_len(v3_sub(cam->pos, e)) < 0.5f;
}

static void submit_chell(game_t const* g, cam_t const* cam, clipset_t const* cs) {
    if (!s_chell_pass) return;
    player_t const* const pl = &g->pl;
    // Part way through a portal: as a cube, drawn twice, each copy cut at
    // its own portal's plane.
    int const             k  = chell_portal(g, pl);
    if (k >= 0) keep_front(&g->portals[k]);
    if (!chell_eye_inside(pl, cam)) submit_chell_at(pl, cam, cs);
    s_keep.n = 0;
    if (k >= 0) {
        s_via[0] = &g->portals[k];
        s_via[1] = &g->portals[k ^ 1];
        keep_front(s_via[1]);
        if (!chell_eye_inside(pl, cam)) submit_chell_at(pl, cam, cs);
        s_keep.n = 0;
        s_via[0] = s_via[1] = NULL;
    }
}

static void set_camera(cam_t const* cam) {
    s_eye = *cam;
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
    s_glow_cells = 0;
    se_light_set(&(se_light_t){.x = s_light.x, .y = s_light.y, .z = s_light.z, .brightness = 0.55f});
    s_ncut = 0;
    for (int i = 0; i < 2; i++)
        if (cut & (1 << i)) portal_behind(&portals[i], cam->pos, &s_cut[s_ncut++]);
    submit_level(cam, cs);
    submit_things(s_game, cam, cs);
    submit_lift_solid(cam, cs);
    submit_chell(s_game, cam, cs);
    // The ghosts of the best runs, where they are -- not one the eye is in.
    for (int i = 0; i < s_ghost_n; i++) {
        if (v3_len(v3_sub(cam->pos, player_eye(&s_ghost_pl[i]))) <= 0.6f) continue;
        s_ghostly = s_ghost_tint[i];
        submit_chell_at(&s_ghost_pl[i], cam, cs);
        s_ghostly = -1;
    }
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
        float const   ov   = level_sheet(q->mat) ? fmodf(s_time * FIZZ_FALL, 1.0f) : 0.0f;  // its streaks fall
        cvert_t const v[4] = {
            {q->origin, 0, ov},
            {v3_add(q->origin, q->du), 1, ov},
            {v3_add(v3_add(q->origin, q->du), q->dv), 1, 1 + ov},
            {v3_add(q->origin, q->dv), 0, 1 + ov},
        };
        submit_quad(v, q->n, cam, cs, m, m->argb, m->flags);
    }
    submit_lift_glass(cam, cs);
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
    s_chell_pass = true;
    draw_pass(target, &v, cs, portals, deeper ? 0 : 1 << which, deeper ? 1 << which : 0, s_deep);
    s_chell_pass = false;
}

void render_frame(pax_buf_t* target, game_t const* g) {
    portal_t const* portals = g->portals;
    s_game                  = g;
    s_stat_passes           = 0;
    s_stat_tris             = 0;
    cam_t const cam         = {player_eye(&g->pl), player_view(&g->pl)};
    s_view                  = cam;
    glow_gather(g);  // what glows lights what is round it

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
