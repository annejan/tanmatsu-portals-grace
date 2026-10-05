#pragma once
// A scripted run through a chamber, as steps: shoot here, walk there.
// Played by demo.c; written in a chamber file's `solution` section
// (chamber.c), or as a C array.

typedef enum {
    OP_END = 0,
    OP_FACE,        // yaw a, pitch b (radians), at once
    OP_SHOOT,       // portal `which` at the point (a, b, c)
    OP_SHOOT_VIEW,  // portal `which` straight along the view
    OP_WALK,        // forward for a seconds
    OP_WALK_TO,     // to (a, c) at pace b, until there, through a portal, or 6 s
    OP_STEP_OFF,    // forward at pace a until off the ground, then let go
    OP_WAIT,        // a seconds
    OP_USE,         // pick up / put down
    OP_FACE_POINT,  // look at (a, b, c), at once
    OP_GRAB,        // look at the nearest free cube and pick it up
    OP_JUMP,        // jump, on the next tick (if on the ground then)
} op_t;

typedef struct {
    op_t  op;
    int   which;
    float a, b, c;
} step_t;

#define SCRIPT_MAX_STEPS 64
