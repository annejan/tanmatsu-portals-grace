#include "cine.h"
#include <math.h>
#include <string.h>
#include "game.h"

#define AIM_BEFORE 0.6f   // s on the aim before the act
#define AIM_AFTER  0.25f  // ... and after it
#define EASE_S     0.16f  // the turns' easing: a Gaussian this wide
#define WALK_PITCH 0.10f  // walking, it looks this far down (rad)

void cine_plan(cine_view_t const* log, int n, float tick, float (*cam)[2], float (*want)[2], bool* aimed) {
    int const acts =
        GAME_EV_SHOT_BLUE | GAME_EV_SHOT_ORANGE | GAME_EV_SHOT_FAIL | GAME_EV_PICKUP | GAME_EV_DROP | GAME_EV_PRESS;
    int const before = (int)(AIM_BEFORE / tick), after = (int)(AIM_AFTER / tick);
    memset(aimed, 0, (size_t)n * sizeof(aimed[0]));
    for (int k = 0; k < n; k++) {
        cine_view_t const* v = &log[k];
        float const        h = sqrtf(v->vx * v->vx + v->vz * v->vz);
        want[k][0]           = v->yaw;
        want[k][1]           = v->pitch;
        if (v->held) {
            // Carrying: the cube is held where the view looks, so it looks
            // there too, or the cube would hang out of the shot.
        } else if (v->ground) {
            // Walking: where it goes, not at its feet.
            if (h > 1.5f) {
                want[k][0] = atan2f(v->vx, v->vz);
                want[k][1] = WALK_PITCH;
            }
        } else if (h > 1.0f || fabsf(v->vy) > 1.5f) {
            // Carried, flung or falling: the way it goes, up or down --
            // not backwards down a funnel, facing where the script last
            // pointed.
            if (h > 1.0f) want[k][0] = atan2f(v->vx, v->vz);
            if (h <= 1.0f && k > 0) want[k][0] = want[k - 1][0];
            float p    = -atanf(v->vy / fmaxf(h, 0.5f)) * 0.6f;
            want[k][1] = p < -0.6f ? -0.6f : p > 0.9f ? 0.9f : p;
        } else if (k > 0) {
            // Hanging still in the air -- at a funnel's end: the same way,
            // but looking down, for where to get off.
            want[k][0] = want[k - 1][0];
            want[k][1] = 0.7f;
        }
    }
    // On the aim before each act, and a little after: the act's own view.
    for (int k = 0; k < n; k++) {
        if (!(log[k].ev & acts)) continue;
        for (int j = k - before; j <= k + after && j < n; j++) {
            if (j < 0) continue;
            // Not back across a portal: the view before it is another room.
            bool crossed = false;
            for (int m = j < k ? j + 1 : k + 1; m <= (j < k ? k : j); m++) crossed |= (log[m].ev & PL_EV_TELEPORT);
            if (crossed) continue;
            want[j][0] = log[k].yaw;
            want[j][1] = log[k].pitch;
            aimed[j]   = true;
        }
    }
    // Eased, each stretch between portals on its own; yaw unwrapped first.
    int const sigma = (int)(EASE_S / tick), reach = 3 * sigma;
    for (int a = 0; a < n;) {
        int b = a + 1;
        while (b < n && !(log[b].ev & PL_EV_TELEPORT)) b++;
        for (int k = a + 1; k < b; k++) {
            float d = want[k][0] - want[k - 1][0];
            while (d > 3.14159265f) d -= 6.2831853f;
            while (d < -3.14159265f) d += 6.2831853f;
            want[k][0] = want[k - 1][0] + d;
        }
        for (int k = a; k < b; k++)
            for (int c = 0; c < 2; c++) {
                float sum = 0.0f, wsum = 0.0f;
                for (int j = k - reach; j <= k + reach; j++) {
                    if (j < a || j >= b) continue;
                    float const w  = expf(-0.5f * (float)((j - k) * (j - k)) / (float)(sigma * sigma));
                    sum           += want[j][c] * w;
                    wsum          += w;
                }
                cam[k][c] = sum / wsum;
                // On the aim at the act itself, exactly: the portal goes
                // where the crosshair is.
                if (aimed[k] && (log[k].ev & acts)) cam[k][c] = want[k][c];
            }
        a = b;
    }
    // A head is never quite still.
    for (int k = 0; k < n; k++) {
        float const t  = (float)k * tick;
        cam[k][0]     += 0.010f * sinf(0.63f * t) + 0.005f * sinf(1.71f * t + 1.0f);
        cam[k][1]     += 0.007f * sinf(0.89f * t + 2.0f);
    }
}
