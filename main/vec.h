#pragma once
// Small 3-vector helpers, header-only so the host tests share them.

#include <math.h>

typedef struct {
    float x, y, z;
} vec3_t;

static inline vec3_t v3(float x, float y, float z) {
    return (vec3_t){x, y, z};
}
static inline vec3_t v3_add(vec3_t a, vec3_t b) {
    return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}
static inline vec3_t v3_sub(vec3_t a, vec3_t b) {
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}
static inline vec3_t v3_scale(vec3_t a, float s) {
    return v3(a.x * s, a.y * s, a.z * s);
}
static inline vec3_t v3_mad(vec3_t a, vec3_t b, float s) {
    return v3(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s);
}
static inline float v3_dot(vec3_t a, vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static inline vec3_t v3_cross(vec3_t a, vec3_t b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3_len(vec3_t a) {
    return sqrtf(v3_dot(a, a));
}
static inline vec3_t v3_norm(vec3_t a) {
    float const l = v3_len(a);
    return l > 1e-9f ? v3_scale(a, 1.0f / l) : v3(0.0f, 0.0f, 0.0f);
}
static inline vec3_t v3_lerp(vec3_t a, vec3_t b, float t) {
    return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

// The engine's camera convention (se_scene.c, camera_build_basis): the
// basis is M = Ry(yaw) * Rx(pitch) * Rz(roll), and its columns are the
// camera's right / up / forward in world space. POSITIVE PITCH LOOKS DOWN.
typedef struct {
    vec3_t right, up, fwd;
} basis_t;

static inline basis_t basis_from_angles(float yaw, float pitch, float roll) {
    float const cy = cosf(yaw), sy = sinf(yaw);
    float const cp = cosf(pitch), sp = sinf(pitch);
    float const cr = cosf(roll), sr = sinf(roll);
    basis_t     b;
    b.right = v3(cy * cr + sy * sp * sr, cp * sr, -sy * cr + cy * sp * sr);
    b.up    = v3(-cy * sr + sy * sp * cr, cp * cr, sy * sr + cy * sp * cr);
    b.fwd   = v3(sy * cp, -sp, cy * cp);
    return b;
}

// The inverse: yaw / pitch / roll that rebuild `b`. Near straight up or
// down roll and yaw share one degree of freedom; roll is then taken as 0
// and yaw carries it, which rebuilds the same basis.
static inline void basis_to_angles(basis_t const* b, float* yaw, float* pitch, float* roll) {
    float fy = -b->fwd.y;
    if (fy > 1.0f) fy = 1.0f;
    if (fy < -1.0f) fy = -1.0f;
    *pitch        = asinf(fy);
    float const c = cosf(*pitch);
    if (c > 1e-4f) {
        *yaw  = atan2f(b->fwd.x, b->fwd.z);
        *roll = atan2f(b->right.y, b->up.y);
    } else {
        // fwd is +-y. With roll = 0: right = (cy, 0, -sy).
        *roll = 0.0f;
        *yaw  = atan2f(-b->right.z, b->right.x);
    }
}
