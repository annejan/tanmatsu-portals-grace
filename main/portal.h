#pragma once
// Portals: where they sit, and the rigid map that carries anything that
// enters one out of the other. Pure C, no engine.
//
// A portal covers two cell faces -- one metre across, two along its `up`
// -- on white panels. Its frame is (right, up, n): n is the face normal,
// pointing out into the room; up is +y on a wall and, on a floor or a
// ceiling, the horizontal axis the player was facing when they shot it;
// right = up x n. Going in through A and out of B turns everything half
// a turn about `up`: what was A's (r, u, n) leaves as B's (-r, u, -n).
// That map has determinant +1, so it never mirrors.

#include <stdbool.h>
#include "level.h"
#include "vec.h"

#define PORTAL_HALF_W 0.5f
#define PORTAL_HALF_H 1.0f

enum { PORTAL_BLUE = 0, PORTAL_ORANGE = 1 };

typedef struct {
    bool   open;
    int    cell[2][3];  // the two solid cells whose face it covers
    int    face;        // dir_t
    vec3_t center, right, up, n;
} portal_t;

// Try to put a portal where a ray from `eye` along `look` first hits the
// level. `other` may not be overlapped. True and `out` filled on success;
// false, `out` untouched, if the surface will not take one.
bool portal_place(level_t const* lv, vec3_t eye, vec3_t look, portal_t const* other, portal_t* out);

// Fill `out` as the portal on (cell, face), with the given `up` -- for
// tests and for setting a chamber up. False if it does not fit there.
bool portal_place_at(level_t const* lv, int x, int y, int z, int face, vec3_t up, portal_t const* other,
                     portal_t* out);

// The four corners, in order round the edge.
void portal_corners(portal_t const* p, vec3_t out[4]);

// The opening is an oval: a PORTAL_OVAL_N-sided polygon inscribed in the
// 1 x 2 rectangle, touching the middle of each side. `scale` grows or
// shrinks it about the centre; points go round in order, starting at
// the right-hand side. The physics still uses the rectangle.
#define PORTAL_OVAL_N 16
void portal_oval(portal_t const* p, float scale, vec3_t out[PORTAL_OVAL_N]);

// A point / a direction / a camera basis carried from `a` to `b`.
vec3_t  portal_map_point(portal_t const* a, portal_t const* b, vec3_t p);
vec3_t  portal_map_dir(portal_t const* a, portal_t const* b, vec3_t d);
basis_t portal_map_basis(portal_t const* a, portal_t const* b, basis_t const* m);

// Portal-local coordinates of a world point: across, along `up`, out.
vec3_t portal_local(portal_t const* p, vec3_t w);

// --- Clip planes ----------------------------------------------------------
//
// What one portal pass may draw: the inside of a convex region bounded by
// planes, where n . x + d >= 0 is kept.
typedef struct {
    vec3_t n;
    float  d;
} plane_t;

// Each view through a portal adds a plane per edge of the oval and one
// for the exit's wall: 17 a level, three levels deep.
#define PORTAL_MAX_PLANES 56

typedef struct {
    plane_t p[PORTAL_MAX_PLANES];
    int     n;
} clipset_t;

// The view through `entry` from `eye`, carried out of `exit`: `in`
// (the region the eye's own pass was limited to, or NULL for the full
// view) narrowed by the four planes through the eye and the entry's
// edges, mapped to the far side, plus the exit's own plane, which cuts
// away everything behind the wall it hangs on.
void portal_clip_through(portal_t const* entry, portal_t const* exit, vec3_t eye, clipset_t const* in,
                         clipset_t* out);

// Clip a convex polygon of `n` vertices against every plane in `cs`.
// Vertices carry texture coordinates along. Returns the new count; 0 if
// nothing is left. `out` needs room for n + cs->n vertices.
typedef struct {
    vec3_t p;
    float  u, v;
} cvert_t;

#define CLIP_MAX_VERTS (4 + PORTAL_MAX_PLANES)

int clip_polygon(clipset_t const* cs, cvert_t const* in, int n, cvert_t* out);
