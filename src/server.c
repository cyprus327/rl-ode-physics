#include <stdio.h>
#include <string.h>

#include <enet/enet.h>

#include "../inc/util.h"
#include "../inc/msgs.h"
#include "../inc/player.h"

typedef struct peerInfo {
    ENetPeer* peer;
    i32 playerID;
} PeerInfo;

static PeerInfo peerInfo[MAX_PLAYERS];

i32 main() {
    if (enet_initialize() != 0) {
        fprintf(stderr, "An error occurred while initializing ENet.\n");
        return EXIT_FAILURE;
    }

    ENetAddress address = { .host = ENET_HOST_ANY, .port = 12345 };
    ENetHost* server = enet_host_create(&address, MAX_PLAYERS, 2, 0, 0);
    if (server == NULL) {
        fprintf(stderr, "An error occurred while trying to create the server.\n");
        return EXIT_FAILURE;
    }

    for (i32 i = 0; i < MAX_PLAYERS; i++) {
        peerInfo[i].peer = NULL;
        players[i].id = peerInfo[i].playerID = -1;
        players[i].pos.x = players[i].pos.y = players[i].dir.x = players[i].dir.y = 0.f;
    }

    printf("Server started on port %d\n", address.port);

    ENetEvent event;
    while (1) {
        while (enet_host_service(server, &event, 1000) > 0) {
            u8 playerUpdated = 0;
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    printf("A new client connected.\n");
                    u8 foundEmpty = 0;
                    for (i32 i = 0; i < MAX_PLAYERS; i++) {
                        if (players[i].id != -1) {
                            continue;
                        }

                        players[i].id = i;
                        players[i].pos = players[i].dir = (Vector3){0.f, 0.f, 0.f};
                        peerInfo[i].playerID = i;
                        peerInfo[i].peer = event.peer;

                        MsgPlayerID idMsg = { .msg = MSGTYPE_C_PLAYER_ID, .playerID = i };
                        ENetPacket* packet = enet_packet_create(&idMsg, sizeof(MsgPlayerID), ENET_PACKET_FLAG_RELIABLE);
                        enet_peer_send(event.peer, 0, packet);
                        playerUpdated = 1;

                        printf("Assigned player ID: %d\n", i);
                        foundEmpty = 1;
                        break;
                    }
                    if (!foundEmpty) {
                        enet_peer_disconnect(event.peer, 0);
                    }
                } break;
                case ENET_EVENT_TYPE_RECEIVE: {
                    switch (*(MsgType*)event.packet->data) {
                        case MSGTYPE_S_PLAYER_UPDATE: {
                            MsgPlayerUpdate* player = (MsgPlayerUpdate*)event.packet->data;
                            players[player->player.id] = player->player;
                            playerUpdated = 1;
                        } break;
                        default: {
                            TraceLog(LOG_WARNING, TextFormat("Received unknown message of length %d", event.packet->dataLength));
                        } break;
                    }
                    enet_packet_destroy(event.packet);
                } break;
                case ENET_EVENT_TYPE_DISCONNECT: {
                    for (i32 i = 0; i < MAX_PLAYERS; i++) {
                        if (event.peer != peerInfo[i].peer) {
                            continue;
                        }

                        players[i].id = -1;
                        peerInfo[i].playerID = -1;
                        peerInfo[i].peer = NULL;
                        playerUpdated = 1;
                        printf("A client disconnected (ID: %d)\n", i);
                        break;
                    }
                } break;
                default: break;
            }

            if (playerUpdated) {
                MsgUpdatePlayers updatedPlayers = { .msg = MSGTYPE_C_UPDATE_PLAYERS };
                memcpy(updatedPlayers.players, players, sizeof(players));
                ENetPacket* packet = enet_packet_create(&updatedPlayers, sizeof(MsgUpdatePlayers), ENET_PACKET_FLAG_RELIABLE);
                enet_host_broadcast(server, 0, packet);
            }
        }
    }

    enet_host_destroy(server);
    enet_deinitialize();
    return EXIT_SUCCESS;
}
