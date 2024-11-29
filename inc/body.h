#pragma once

#include "raylib.h"
#include "ode/ode.h"

#define MAX_BODIES 512

typedef enum collMask {
    CMASK_MAP = 1,
    CMASK_OBJ = 2,
    CMASK_ALL = ~0
} CollMask;

typedef enum bodyType {
    BODYTYPE_NULL,
    BODYTYPE_SPHERE,
    BODYTYPE_BOX
} BodyType;

typedef struct body {
    dBodyID body;
    dGeomID geom;
    BodyType type;
} Body;

typedef struct bodyState {
    BodyType type;
    Vector3 pos, rot, size;
    Color col;
} BodyState;

typedef struct renderBody {
    BodyState state;
    Model display;
} RenderBody;
