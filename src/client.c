#include <stdio.h>

#include <enet/enet.h>

#define ERROR_EXIT(err) { fprintf(stderr, (err)); exit(EXIT_FAILURE); }

int main(int argc, char **argv) {
    if (enet_initialize() != 0) {
        ERROR_EXIT("An error occurred while initializing ENet");
    }
    atexit(enet_deinitialize);

    ENetHost* client = enet_host_create(NULL, 1, 2, 0, 0);
    if (client == NULL) {
        ERROR_EXIT("An error occurred while trying to create an ENet client");
    }

    ENetAddress address;
    enet_address_set_host(&address, "127.0.0.1");
    address.port = 12345;

    ENetPeer* peer = enet_host_connect(client, &address, 2, 0);
    if (peer == NULL) {
        ERROR_EXIT("No available peers for initiating an ENet connection");
    }

    ENetEvent event;
    if (enet_host_service(client, &event, 5000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
        printf("Connection to server succeeded\n");
    } else {
        printf("Connection to server failed\n");
        enet_peer_reset(peer);
    }

    enet_host_destroy(client);
    return 0;
}
