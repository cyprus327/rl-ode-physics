#pragma once

#include "raylib.h"
#include "ode/common.h"

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
    dReal transform[16];
    Vector3 size;
    Color col;
} BodyState;

typedef struct renderBody {
    BodyState state;
    Model display;
} RenderBody;
