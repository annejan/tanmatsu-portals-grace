#include "portal.h"
#include <math.h>
#include <string.h>

// The face centre of (cell, face).
static vec3_t face_center(int x, int y, int z, int face) {
    vec3_t const n = dir_vec(face);
    return v3((float)x + 0.5f + 0.5f * n.x, (float)y + 0.5f + 0.5f * n.y, (float)z + 0.5f + 0.5f * n.z);
}

static bool face_takes_portal(level_t const* lv, int x, int y, int z, int face) {
    if (!level_portalable(lv, x, y, z)) return false;
    // A button's pad covers the top of the cell it stands on.
    if (face == DIR_PY)
        for (int i = 0; i < lv->n_buttons; i++)
            if (lv->buttons[i].x == x && lv->buttons[i].y == y && lv->buttons[i].z == z) return false;
    int dx, dy, dz;
    dir_step(face, &dx, &dy, &dz);
    return !level_solid(lv, x + dx, y + dy, z + dz);
}

static bool overlaps(portal_t const* p, portal_t const* other) {
    if (other == NULL || !other->open || other->face != p->face) return false;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            if (memcmp(p->cell[i], other->cell[j], sizeof(p->cell[i])) == 0) return true;
    return false;
}

bool portal_place_at(level_t const* lv, int x, int y, int z, int face, vec3_t up, portal_t const* other,
                     portal_t* out) {
    int const ux = (int)up.x, uy = (int)up.y, uz = (int)up.z;
    portal_t  p  = {0};
    p.open       = true;
    p.face       = face;
    p.cell[0][0] = x;
    p.cell[0][1] = y;
    p.cell[0][2] = z;
    p.cell[1][0] = x + ux;
    p.cell[1][1] = y + uy;
    p.cell[1][2] = z + uz;
    for (int i = 0; i < 2; i++)
        if (!face_takes_portal(lv, p.cell[i][0], p.cell[i][1], p.cell[i][2], face)) return false;
    if (overlaps(&p, other)) return false;
    p.n      = dir_vec(face);
    p.up     = up;
    p.right  = v3_cross(up, p.n);
    p.center = v3_scale(v3_add(face_center(x, y, z, face), face_center(x + ux, y + uy, z + uz, face)), 0.5f);
    *out     = p;
    return true;
}

bool portal_place(level_t const* lv, vec3_t eye, vec3_t look, portal_t const* other, portal_t* out) {
    ray_hit_t const h = level_raycast(lv, eye, look, 64.0f);
    if (!h.hit) return false;
    vec3_t const n = dir_vec(h.face);

    // Which way is up for it: +y on a wall; on a floor or a ceiling, the
    // horizontal axis nearest the way the player looks.
    vec3_t up;
    if (n.y == 0.0f) {
        up = v3(0, 1, 0);
    } else if (fabsf(look.x) > fabsf(look.z)) {
        up = v3(look.x > 0 ? 1.0f : -1.0f, 0, 0);
    } else {
        up = v3(0, 0, look.z > 0 ? 1.0f : -1.0f);
    }

    vec3_t const down = v3_scale(up, -1.0f);
    int const    dx = (int)down.x, dy = (int)down.y, dz = (int)down.z;

    // On a wall: the lower of the two pairs that hold the cell hit, so a
    // shot at eye height leaves the portal standing on the floor, where
    // it can be walked into, rather than hanging a metre up the wall.
    if (n.y == 0.0f) {
        if (portal_place_at(lv, h.x + dx, h.y + dy, h.z + dz, h.face, up, other, out)) return true;
        return portal_place_at(lv, h.x, h.y, h.z, h.face, up, other, out);
    }

    // On a floor or a ceiling: the cell hit and its neighbour on the side
    // of it the shot landed on, so the portal centres near the crosshair.
    float const sign  = up.x + up.y + up.z;  // +1 or -1: up is one axis
    float const coord = v3_dot(h.point, v3_scale(up, sign));
    float const frac  = coord - floorf(coord);
    bool const  upper = sign > 0 ? frac >= 0.5f : frac < 0.5f;
    if (upper) {
        if (portal_place_at(lv, h.x, h.y, h.z, h.face, up, other, out)) return true;
        return portal_place_at(lv, h.x + dx, h.y + dy, h.z + dz, h.face, up, other, out);
    }
    if (portal_place_at(lv, h.x + dx, h.y + dy, h.z + dz, h.face, up, other, out)) return true;
    return portal_place_at(lv, h.x, h.y, h.z, h.face, up, other, out);
}

void portal_corners(portal_t const* p, vec3_t out[4]) {
    vec3_t const r = v3_scale(p->right, PORTAL_HALF_W);
    vec3_t const u = v3_scale(p->up, PORTAL_HALF_H);
    out[0]         = v3_sub(v3_sub(p->center, r), u);
    out[1]         = v3_sub(v3_add(p->center, r), u);
    out[2]         = v3_add(v3_add(p->center, r), u);
    out[3]         = v3_add(v3_sub(p->center, r), u);
}

void portal_oval(portal_t const* p, float scale, vec3_t out[PORTAL_OVAL_N]) {
    for (int i = 0; i < PORTAL_OVAL_N; i++) {
        float const a = 6.2831853f * (float)i / (float)PORTAL_OVAL_N;
        out[i]        = v3_add(p->center, v3_add(v3_scale(p->right, cosf(a) * PORTAL_HALF_W * scale),
                                                 v3_scale(p->up, sinf(a) * PORTAL_HALF_H * scale)));
    }
}

vec3_t portal_local(portal_t const* p, vec3_t w) {
    vec3_t const d = v3_sub(w, p->center);
    return v3(v3_dot(d, p->right), v3_dot(d, p->up), v3_dot(d, p->n));
}

vec3_t portal_map_dir(portal_t const* a, portal_t const* b, vec3_t d) {
    float const lr = v3_dot(d, a->right), lu = v3_dot(d, a->up), ln = v3_dot(d, a->n);
    return v3_add(v3_add(v3_scale(b->right, -lr), v3_scale(b->up, lu)), v3_scale(b->n, -ln));
}

vec3_t portal_map_point(portal_t const* a, portal_t const* b, vec3_t p) {
    return v3_add(b->center, portal_map_dir(a, b, v3_sub(p, a->center)));
}

basis_t portal_map_basis(portal_t const* a, portal_t const* b, basis_t const* m) {
    return (basis_t){portal_map_dir(a, b, m->right), portal_map_dir(a, b, m->up), portal_map_dir(a, b, m->fwd)};
}

// --- Clip planes ------------------------------------------------------

static plane_t plane_map(portal_t const* a, portal_t const* b, plane_t pl) {
    vec3_t const n  = portal_map_dir(a, b, pl.n);
    vec3_t const p0 = portal_map_point(a, b, v3_scale(pl.n, -pl.d));
    return (plane_t){n, -v3_dot(n, p0)};
}

// The plane through three points, turned so `inside` is kept.
static plane_t plane_through(vec3_t a, vec3_t b, vec3_t c, vec3_t inside) {
    vec3_t const n = v3_norm(v3_cross(v3_sub(b, a), v3_sub(c, a)));
    plane_t      p = {n, -v3_dot(n, a)};
    if (v3_dot(p.n, inside) + p.d < 0.0f) {
        p.n = v3_scale(p.n, -1.0f);
        p.d = -p.d;
    }
    return p;
}

void portal_clip_through(portal_t const* entry, portal_t const* exit, vec3_t eye, clipset_t const* in, clipset_t* out) {
    static clipset_t cs;  // static: three of these deep is a lot of stack
    cs.n = 0;
    if (in != NULL) cs = *in;
    vec3_t c[PORTAL_OVAL_N];
    portal_oval(entry, 1.0f, c);
    // A point on the ray from the eye through the hole's middle, a little
    // beyond it: inside all the edge planes however obliquely the eye looks.
    vec3_t const inside = v3_mad(entry->center, v3_sub(entry->center, eye), 0.1f);
    for (int i = 0; i < PORTAL_OVAL_N && cs.n < PORTAL_MAX_PLANES - 1; i++) {
        cs.p[cs.n++] = plane_through(eye, c[i], c[(i + 1) % PORTAL_OVAL_N], inside);
    }
    out->n = 0;
    for (int i = 0; i < cs.n; i++) out->p[out->n++] = plane_map(entry, exit, cs.p[i]);
    // The exit's wall and all behind it. A hair in front of the plane,
    // so the wall the exit hangs on -- coplanar with it -- goes too.
    out->p[out->n++] = (plane_t){exit->n, -v3_dot(exit->n, exit->center) - 0.002f};
}

void portal_behind(portal_t const* p, vec3_t eye, clipset_t* out) {
    out->n           = 0;
    // Behind the plane, by a hair: the wall the portal hangs on, in the
    // plane itself, stays.
    out->p[out->n++] = (plane_t){v3_scale(p->n, -1.0f), v3_dot(p->n, p->center) - 0.002f};
    vec3_t c[PORTAL_OVAL_N];
    portal_oval(p, 1.0f, c);
    vec3_t const inside = v3_mad(p->center, v3_sub(p->center, eye), 0.1f);
    for (int i = 0; i < PORTAL_OVAL_N && out->n < PORTAL_MAX_PLANES; i++)
        out->p[out->n++] = plane_through(eye, c[i], c[(i + 1) % PORTAL_OVAL_N], inside);
}

// Split `in` by plane `pl`: the part where n . x + d >= 0 to `keep`, the
// rest to `drop`. Returns the two counts.
static void split(plane_t const* pl, cvert_t const* in, int n, cvert_t* keep, int* nk, cvert_t* drop, int* nd) {
    *nk = *nd = 0;
    for (int i = 0; i < n; i++) {
        cvert_t const* a  = &in[i];
        cvert_t const* b  = &in[(i + 1) % n];
        float const    da = v3_dot(pl->n, a->p) + pl->d;
        float const    db = v3_dot(pl->n, b->p) + pl->d;
        if (da >= 0.0f) {
            if (*nk < CLIP_MAX_VERTS) keep[(*nk)++] = *a;
        } else {
            if (*nd < CLIP_MAX_VERTS) drop[(*nd)++] = *a;
        }
        if ((da >= 0.0f) != (db >= 0.0f)) {
            float const   t = da / (da - db);
            cvert_t const m = {v3_lerp(a->p, b->p, t), a->u + (b->u - a->u) * t, a->v + (b->v - a->v) * t};
            if (*nk < CLIP_MAX_VERTS) keep[(*nk)++] = m;
            if (*nd < CLIP_MAX_VERTS) drop[(*nd)++] = m;
        }
    }
}

void clip_subtract(clipset_t const* r, cvert_t const* in, int n, int level, poly_fn emit, void* ctx) {
    // Static, one set per level: an emit may subtract again at level + 1.
    static cvert_t buf[2][3][CLIP_MAX_VERTS];
    cvert_t*       cur  = buf[level][0];
    cvert_t*       next = buf[level][1];
    cvert_t*       out  = buf[level][2];
    memcpy(cur, in, (size_t)n * sizeof(cvert_t));
    // Peel off, plane by plane, what lies outside the region; what is left
    // after the last plane is inside it, and goes.
    for (int k = 0; k < r->n; k++) {
        int nk, nd;
        split(&r->p[k], cur, n, next, &nk, out, &nd);
        if (nd >= 3) emit(out, nd, ctx);
        if (nk < 3) return;
        cvert_t* const t = cur;
        cur              = next;
        next             = t;
        n                = nk;
    }
}

int clip_polygon(clipset_t const* cs, cvert_t const* in, int n, cvert_t* out) {
    static cvert_t buf[2][CLIP_MAX_VERTS];  // static: off the task's stack
    memcpy(buf[0], in, (size_t)n * sizeof(cvert_t));
    int cur = 0;
    for (int k = 0; k < cs->n && n > 0; k++) {
        plane_t const* pl  = &cs->p[k];
        cvert_t const* src = buf[cur];
        cvert_t*       dst = buf[cur ^ 1];
        int            m   = 0;
        for (int i = 0; i < n; i++) {
            cvert_t const* a  = &src[i];
            cvert_t const* b  = &src[(i + 1) % n];
            float const    da = v3_dot(pl->n, a->p) + pl->d;
            float const    db = v3_dot(pl->n, b->p) + pl->d;
            if (da >= 0.0f && m < CLIP_MAX_VERTS) dst[m++] = *a;
            if ((da >= 0.0f) != (db >= 0.0f) && m < CLIP_MAX_VERTS) {
                float const t = da / (da - db);
                dst[m++]      = (cvert_t){v3_lerp(a->p, b->p, t), a->u + (b->u - a->u) * t, a->v + (b->v - a->v) * t};
            }
        }
        n    = m;
        cur ^= 1;
    }
    memcpy(out, buf[cur], (size_t)n * sizeof(cvert_t));
    return n;
}
