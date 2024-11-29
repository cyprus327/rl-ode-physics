#pragma once

#include "body.h"
#include "player.h"

typedef enum msgType {
    MSGTYPE_C_PLAYER_ID,
    MSGTYPE_C_UPDATE_PLAYERS,
    MSGTYPE_S_PLAYER_UPDATE,

    MSGTYPE_C_UPDATE_BODIES,
    MSGTYPE_S_NEW_BODY
} MsgType;

typedef struct msgPlayerID {
    MsgType msg;
    i32 playerID;
} MsgPlayerID;

typedef struct msgPlayerUpdate {
    MsgType msg;
    PlayerState player;
} MsgPlayerUpdate;

typedef struct msgUpdatePlayers {
    MsgType msg;
    PlayerState players[MAX_PLAYERS];
} MsgUpdatePlayers;

typedef struct msgBodyInfo {
    MsgType msg;
    BodyState bodies[MAX_BODIES];
} MsgUpdateBodies;

typedef struct msgNewBody {
    MsgType msg;
    BodyState body;
} MsgNewBody;
