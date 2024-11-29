#pragma once

#include "raylib.h"
#include "raymath.h"

#include "util.h"

#define MAX_PLAYERS 32

typedef struct playerState {
    Vector3 pos, dir;
    i32 id;
} PlayerState;

extern PlayerState players[MAX_PLAYERS];
extern i32 localID;
extern Camera playerCam;

void Player_UpdateLocal(f32 moveSpeed, f32 turnSpeed, f32 dt);
