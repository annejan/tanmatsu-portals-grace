#pragma once
// Upright boxes moving through the chamber: the player and the cubes.
// Collision with the cell grid and with other boxes, the tunnel behind
// an open portal, and going through one with momentum kept. Pure C, no
// engine.

#include <stdbool.h>
#include "level.h"
#include "portal.h"
#include "vec.h"

typedef struct {
    vec3_t pos;    // centre of the base
    vec3_t vel;
    float  hw, h;  // half width (x and z), height
    float  probe;  // height of the point whose crossing a portal's plane teleports the box
    bool   on_ground;
} body_t;

typedef struct {
    vec3_t lo, hi;
} aabb_t;

typedef struct {
    level_t const*  lv;
    portal_t const* portals;  // the pair; linked only while both are open
    aabb_t const*   boxes;    // other solid boxes (cubes), may be NULL
    int             n_boxes;
} phys_world_t;

#define PHYS_GRAVITY  15.0f
#define PHYS_MAX_FALL 30.0f

enum { PHYS_TELEPORT = 1 << 0, PHYS_LANDED = 1 << 1 };

// Move `b` by its velocity for `dt` seconds. Gravity and steering are the
// caller's. Returns PHYS_* bits; on PHYS_TELEPORT, `*through` is the index
// of the portal it went in by (its position and velocity are already on
// the far side). `*impact` is the downward speed it landed at.
int body_move(body_t* b, phys_world_t const* w, float dt, int* through, float* impact);

static inline vec3_t body_center(body_t const* b) {
    return v3(b->pos.x, b->pos.y + b->h * 0.5f, b->pos.z);
}

static inline aabb_t body_aabb(body_t const* b) {
    return (aabb_t){v3(b->pos.x - b->hw, b->pos.y, b->pos.z - b->hw), v3(b->pos.x + b->hw, b->pos.y + b->h, b->pos.z + b->hw)};
}

static inline bool aabb_overlap(aabb_t const* a, aabb_t const* b) {
    return a->lo.x < b->hi.x && a->hi.x > b->lo.x && a->lo.y < b->hi.y && a->hi.y > b->lo.y && a->lo.z < b->hi.z &&
           a->hi.z > b->lo.z;
}
