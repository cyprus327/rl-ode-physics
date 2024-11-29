#include <math.h>
#include <stdio.h>
#include <string.h>

#include <raylib.h>
#include <enet/enet.h>

#include "../inc/util.h"
#include "../inc/msgs.h"
#include "../inc/player.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

i32 main() {
    if (enet_initialize() != 0) {
        fprintf(stderr, "An error occurred while initializing ENet.\n");
        return EXIT_FAILURE;
    }

    ENetHost* client = enet_host_create(NULL, 1, 2, 0, 0);

    if (client == NULL) {
        fprintf(stderr, "An error occurred while trying to create the client\n");
        return EXIT_FAILURE;
    }

    ENetAddress address;
    enet_address_set_host(&address, "127.0.0.1");
    address.port = 12345;

    ENetPeer* peer = enet_host_connect(client, &address, 2, 0);
    if (peer == NULL) {
        fprintf(stderr, "No available peers for initiating a connection\n");
        return EXIT_FAILURE;
    }

    for (i32 i = 0; i < MAX_PLAYERS; i++) {
        players[i].id = -1;
    }

    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Multiplayer Game");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        ENetEvent event;
        while (enet_host_service(client, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE: {
                    switch (*(MsgType*)event.packet->data) {
                        case MSGTYPE_C_PLAYER_ID: {
                            if (localID != -1) {
                                break;
                            }
                            const MsgPlayerID* idMsg = (MsgPlayerID*)event.packet->data;
                            const i32 id = idMsg->playerID;
                            players[id].id = id;
                            localID = id;
                            printf("RECEIVED ID: %d\n", id);
                        } break;
                        case MSGTYPE_C_UPDATE_PLAYERS: {
                            const MsgUpdatePlayers* updateMsg = (MsgUpdatePlayers*)event.packet->data;
                            for (i32 i = 0; i < MAX_PLAYERS; i++) {
                                if (i != localID) {
                                    players[i] = updateMsg->players[i];
                                }
                            }
                        } break;
                        default: break;
                    }
                    enet_packet_destroy(event.packet);
                } break;
                default: {
                    printf("UNKNOWN EVENT\n");
                } break;
            }
        }

        if (localID != -1) {
            PlayerState* localPlayer = &players[localID];

            const f32 moveSpeed = 300.f * GetFrameTime();
            const f32 turnSpeed = 10.f * GetFrameTime();

            if (IsKeyDown(KEY_W)) localPlayer->pos.y -= moveSpeed;
            if (IsKeyDown(KEY_S)) localPlayer->pos.y += moveSpeed;
            if (IsKeyDown(KEY_A)) localPlayer->pos.x -= moveSpeed;
            if (IsKeyDown(KEY_D)) localPlayer->pos.x += moveSpeed;

            localPlayer->dir.x += (IsKeyDown(KEY_LEFT) ? -turnSpeed : 0.f) + (IsKeyDown(KEY_RIGHT) ? turnSpeed : 0.f);

            MsgPlayerUpdate msg = { .msg = MSGTYPE_S_PLAYER_UPDATE, .player = *localPlayer };
            ENetPacket *packet = enet_packet_create(&msg, sizeof(MsgPlayerUpdate), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(peer, 0, packet);
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        for (i32 i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].id == -1) {
                continue;
            }

            DrawCircleV((Vector2){players[i].pos.x, players[i].pos.y}, 10, i == localID ? RED : BLUE);
            DrawLineEx(
                (Vector2){players[i].pos.x, players[i].pos.y},
                (Vector2){players[i].pos.x + cosf(players[i].dir.x) * 20,
                            players[i].pos.y + sinf(players[i].dir.x) * 20},
                2, i == localID ? BLACK : BLUE
            );
        }

        EndDrawing();
    }

    CloseWindow();
    enet_peer_disconnect(peer, 0);
    enet_host_destroy(client);
    enet_deinitialize();
    return EXIT_SUCCESS;
}
