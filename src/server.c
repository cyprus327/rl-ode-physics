#include <stdio.h>
#include <stdlib.h>

#include "raylib.h"

#include "enet/enet.h"

#define ERROR_EXIT(err) { fprintf(stderr, (err)); exit(EXIT_FAILURE); }

int main(void) {
    if (enet_initialize()) {
        ERROR_EXIT("enet_initialize error\n");
    }
    atexit(enet_deinitialize);

    ENetAddress addr;
    ENetHost* server = enet_host_create(&addr, 32, 2, 0, 0);
    if (!server) {
        ERROR_EXIT("Error when creating host\n");
    }

    printf("Server started on port: %u\n", addr.port);

    while (1) {
        ENetEvent event;
        while (enet_host_service(server, &event, 1000) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    printf("A new client has connected from %x:%u\n",
                        event.peer->address.host, event.peer->address.port);
                    event.peer->data = (void*)TextFormat("Client %u", event.peer->address.port);
                } break;
                case ENET_EVENT_TYPE_RECEIVE: {
                    printf("A packet of length %zu was received from %s on channel %u\n",
                        event.packet->dataLength, (char*)event.peer->data, event.channelID);
                    enet_packet_destroy(event.packet);
                } break;
                case ENET_EVENT_TYPE_DISCONNECT: {
                    printf("%s disconnected\n", (char*)event.peer->data);
                    event.peer->data = NULL;
                } break;
                default: break;
            }
        }
    }

    enet_host_destroy(server);
    return 0;
}
