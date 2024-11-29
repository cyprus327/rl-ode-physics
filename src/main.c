#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#define RAYGUI_IMPLEMENTATION
#include "../inc/raygui.h"
#include "ode/ode.h"
#include "enet/enet.h"

#include "../inc/util.h" // shouldn't have to do this but zed sucks for some reason
#include "../inc/rand.h"
#include "../inc/msgs.h"
#include "../inc/player.h"

#define MAX_PITCH (89.f * DEG2RAD)

#define SHADOWMAP_RESOLUTION 2048

typedef struct peerInfo {
    ENetPeer* peer;
    i32 playerID;
} PeerInfo;

static dWorldID world;
static dSpaceID space;
static dJointGroupID contactGroup;

static Shader shadowShader;

static ENetHost* host;
static ENetPeer* peer;

static void NearCallback(void* data, dGeomID o1, dGeomID o2);
static i32 AddBody(Body* bodies, BodyState* states, CollMask category, CollMask collide, BodyState state, i8 isKinematic);
static i32 AddBodyMap(Body* bodies, BodyState* states, Vector3 pos, Vector3 rot, Vector3 size, Color col);
static void ReleaseBody(RenderBody* bodies, i32 id);

static void ClientAddBody(BodyState body);

// all shadowmap stuff copied from the raylib example shadowmap project
static RenderTexture LoadShadowmapRenderTexture(i32 width, i32 height);
static void UnloadShadowmapRenderTexture(RenderTexture2D target);

static i8 StartServer(void) {
    if (enet_initialize() != 0) {
        TraceLog(LOG_ERROR, "enet initialization error\n");
        return 1;
    }

    atexit(enet_deinitialize);

    const ENetAddress address = { .host = ENET_HOST_ANY, .port = 12345 };
    host = enet_host_create(&address, MAX_PLAYERS, 2, 0, 0);
    if (!host) {
        TraceLog(LOG_ERROR, "server creation error\n");
        return 1;
    }

    printf("Server started on port %u.\n", host->address.port);

    struct ifaddrs* interfaces;
    if (getifaddrs(&interfaces) == 0) {
        printf("Server available on the following IP addresses:\n");
        for (struct ifaddrs* ifa = interfaces; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) { // Only IPv4 addresses
                char ip[INET_ADDRSTRLEN];
                struct sockaddr_in *sockAddr = (struct sockaddr_in *)ifa->ifa_addr;
                inet_ntop(AF_INET, &sockAddr->sin_addr, ip, INET_ADDRSTRLEN);
                printf("  %s (%s)\n", ip, ifa->ifa_name);
            }
        }
        freeifaddrs(interfaces);
    } else {
        perror("getifaddrs");
    }

    dInitODE();
    world = dWorldCreate();
    dWorldSetGravity(world, 0.0, -9.8, 0.0);
    space = dHashSpaceCreate(0);
    contactGroup = dJointGroupCreate(0);

    PeerInfo peerInfo[MAX_PLAYERS];
    for (i32 i = 0; i < MAX_PLAYERS; i++) {
        peerInfo[i].peer = NULL;
        peerInfo[i].playerID = -1;
    }

    Body bodies[MAX_BODIES];
    BodyState bodyStates[MAX_BODIES];
    for (i32 i = 0; i < MAX_BODIES; i++) {
        bodies[i].type = bodyStates[i].type = BODYTYPE_NULL;
    }

    // const Texture texture = LoadTexture("res/grassTexture.png");
    // SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);

    const i32 mainFloor = AddBodyMap(bodies, bodyStates, (Vector3){0.f, 0.f, 0.f}, (Vector3){0.f, 0.f, 0.f}, (Vector3){100.f, 1.f, 100.f}, DARKGRAY);
    // bodies[mainFloor].display.materials->maps[MATERIAL_MAP_DIFFUSE].texture = texture;

    // AddBodyMap((Vector3){4.f, 3.f, 0.f}, (Vector3){0.f, 0.f, -0.5f}, (Vector3){0.5f, 8.f, 12.f});
    // AddBodyMap((Vector3){-4.f, 3.f, 0.f}, (Vector3){0.f, 0.f, 0.5f}, (Vector3){0.5f, 8.f, 12.f});
    AddBodyMap(bodies, bodyStates, (Vector3){0.f, 3.f, 6.f}, (Vector3){0.f, 0.f, 0.f}, (Vector3){12.f, 8.f, 0.5f}, GREEN);
    AddBodyMap(bodies, bodyStates, (Vector3){0.f, 3.f, -6.f}, (Vector3){0.f, 0.f, 0.f}, (Vector3){12.f, 8.f, 0.5f}, BLUE);

    ENetEvent event;
    while (!WindowShouldClose()) {
        static const char* info = "Nothing has happened yet";
        while (enet_host_service(host, &event, 500) > 0) {
            u32 start = enet_time_get();

            u8 playerUpdated = 0;
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    info = TextFormat("A new client connected from %x:%u\n%s", event.peer->address.host, event.peer->address.port, info);
                    u8 foundEmpty = 0;
                    for (i32 i = 0; i < MAX_PLAYERS; i++) {
                        if (players[i].id != -1) {
                            continue;
                        }

                        peerInfo[i].peer = event.peer;
                        peerInfo[i].playerID = players[i].id = i;
                        players[i].pos = players[i].dir = (Vector3){0.f, 0.f, 0.f};

                        MsgPlayerID idMsg = { .msg = MSGTYPE_C_PLAYER_ID, .playerID = i };
                        ENetPacket* packet = enet_packet_create(&idMsg, sizeof(MsgPlayerID), ENET_PACKET_FLAG_RELIABLE);
                        enet_peer_send(event.peer, 0, packet);

                        info = TextFormat("Assigned ID: %d\n%s", i, info);

                        foundEmpty = playerUpdated = 1;
                        break;
                    }
                    if (!foundEmpty) {
                        enet_peer_disconnect(event.peer, 0);
                        info = TextFormat("Server full, disconnected client\n%s", info);
                    }
                } break;
                case ENET_EVENT_TYPE_RECEIVE: {
                    info = TextFormat("Packet received from client on channel %u\n", event.channelID);
                    switch (*(MsgType*)event.packet->data) {
                        case MSGTYPE_S_PLAYER_UPDATE: {
                            MsgPlayerUpdate* player = (MsgPlayerUpdate*)event.packet->data;
                            players[player->player.id] = player->player;
                            playerUpdated = 1;
                            info = TextFormat("Updated player %d\n%s", player->player.id, info);
                        } break;
                        case MSGTYPE_S_NEW_BODY: {
                            MsgNewBody* body = (MsgNewBody*)event.packet->data;
                            const BodyState state = body->body;
                            const i32 id = AddBody(bodies, bodyStates, CMASK_OBJ, CMASK_OBJ | CMASK_MAP, state, 0);
                        } break;
                        default: {
                            info = TextFormat("Unknown message type\n%s", info);
                        } break;
                    }
                    enet_packet_destroy(event.packet);
                } break;
                case ENET_EVENT_TYPE_DISCONNECT: {
                    for (i32 i = 0; i < MAX_PLAYERS; i++) {
                        if (event.peer != peerInfo[i].peer) {
                            continue;
                        }
                        players[i].id = peerInfo[i].playerID = -1;
                        peerInfo[i].peer = NULL;
                        playerUpdated = 1;
                        info = TextFormat("Client disconnected\n");
                        break;
                    }
                } break;
                default: {
                    info = TextFormat("Unknown event");
                } break;
            }

            if (playerUpdated) {
                MsgUpdatePlayers updatedPlayers = { .msg = MSGTYPE_C_UPDATE_PLAYERS };
                memcpy(updatedPlayers.players, players, sizeof(players));
                ENetPacket* packet = enet_packet_create(&updatedPlayers, sizeof(MsgUpdatePlayers), ENET_PACKET_FLAG_RELIABLE);
                enet_host_broadcast(host, 0, packet);
            }

            static u32 counter = 0;
            u32 delta = enet_time_get() - start;
            if (delta > 2) {
                printf("C: %u, D: %u\n", counter, delta);
            }
            counter += 0 == delta ? 1 : delta;
            if (counter > 10) {
                dSpaceCollide(space, NULL, NearCallback);
                dWorldStep(world, counter / 1000.0);
                dJointGroupEmpty(contactGroup);
                counter = 0;

                for (i32 i = 0; i < MAX_BODIES; i++) {
                    if (BODYTYPE_NULL == bodies[i].type) {
                        continue;
                    }

                    const dReal* pos;
                    const dReal* rot;
                    if (bodies[i].body) {
                        pos = dBodyGetPosition(bodies[i].body);
                        rot = dBodyGetRotation(bodies[i].body);
                    } else {
                        pos = dGeomGetPosition(bodies[i].geom);
                        rot = dGeomGetRotation(bodies[i].geom);
                    }

                    bodyStates[i].pos = (Vector3){pos[0], pos[1], pos[2]};
                    bodyStates[i].rot = (Vector3){0.f, 0.f, 0.f}; // TODO probably just send rotation matrix
                }

                MsgUpdateBodies updatedBodies = { .msg = MSGTYPE_C_UPDATE_BODIES };
                memcpy(updatedBodies.bodies, bodyStates, sizeof(bodyStates));
                ENetPacket* packet = enet_packet_create(&updatedBodies, sizeof(MsgUpdateBodies), ENET_PACKET_FLAG_RELIABLE);
                enet_host_broadcast(host, 0, packet);
            }

            BeginDrawing();
            ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
                DrawText(info, 100 + 50 * sinf(GetTime()), 100, 40, GetColor(GuiGetStyle(DEFAULT, TEXT_COLOR_NORMAL)));
            EndDrawing();
        }

        BeginDrawing();
        ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
            DrawText(info, 100 + 50 * sinf(GetTime()), 100, 40, GetColor(GuiGetStyle(DEFAULT, TEXT_COLOR_NORMAL)));
        EndDrawing();
    }

    enet_host_destroy(host);
    for (i32 i = 0; i < MAX_BODIES; i++) {
        if (bodies[i].body) {
            dBodyDestroy(bodies[i].body);
        }
        dGeomDestroy(bodies[i].geom);
    }
    dJointGroupDestroy(contactGroup);
    dWorldDestroy(world);
    dCloseODE();
    CloseWindow();
    return 0;
}

static u8 JoinServer(const i8* ip, const i8* port) {
    if (enet_initialize() != 0) {
        TraceLog(LOG_ERROR, "Error while initializing enet\n");
        return 1;
    }

    atexit(enet_deinitialize);

    ENetAddress address;
    host = enet_host_create(NULL, 1, 2, 0, 0);
    if (!host) {
        TraceLog(LOG_ERROR, "Error while trying to create the client host\n");
        return 1;
    }

    enet_address_set_host(&address, ip);
    address.port = atoi(port);

    peer = enet_host_connect(host, &address, 2, 0); // 2 channels
    if (!peer) {
        TraceLog(LOG_ERROR, "No available peers for initiating an enet connection\n");
        return 1;
    }

    return 0;
}

static void DrawScene(RenderBody* bodies) {
    for (i32 i = 0; i < MAX_BODIES; i++) {
        if (bodies[i].state.type == BODYTYPE_NULL) {
            continue;
        }

        const Vector3 pos = bodies[i].state.pos;
        const Vector3 rot = bodies[i].state.rot;

        // TODO fix this
        // bodies[i].display.transform = (Matrix){
        //     0.f, 0.f, 0.f, pos.x,
        //     0.f, 0.f, 0.f, pos.y,
        //     0.f, 0.f, 0.f, pos.z,
        //     0.f, 0.f, 0.f, 1.f
        // };
        // bodies[i].display.transform = (Matrix){
        //     rot[0], rot[1], rot[2], pos[0],
        //     rot[4], rot[5], rot[6], pos[1],
        //     rot[8], rot[9], rot[10], pos[2],
        //     0.f, 0.f, 0.f, 1.f
        // };

        DrawModel(bodies[i].display, pos, 1.f, bodies[i].state.col);
    }

    for (i32 i = 0; i < MAX_PLAYERS; i++) {
        if (i == localID || players[i].id == -1) {
            continue;
        }

        // printf("DRAWING PLAYER %d AT: %f %f %f\n", i, players[i].pos.x, players[i].pos.y, players[i].pos.z);
        DrawSphere(players[i].pos, 0.5f, BLUE);
        DrawLine3D(players[i].pos, Vector3Add(players[i].pos, Vector3Scale(players[i].dir, 5.f)), RED);
    }
}

i32 main(void) {
    SetTargetFPS(1000);
    SetExitKey(KEY_RIGHT_SHIFT);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Window");
    GuiLoadStyleJungle();
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);

    randState = (u32)time(NULL);

    for (i32 i = 0; i < MAX_PLAYERS; i++) {
        players[i].id = -1;
        players[i].pos = players[i].dir = (Vector3){0.f, 0.f, 0.f};
    }

    RenderBody bodies[MAX_BODIES];
    for (i32 i = 0; i < MAX_BODIES; i++) {
        bodies[i].state.type = BODYTYPE_NULL;
    }

    shadowShader = LoadShader("res/shadowMap.vert", "res/shadowMap.frag");
    shadowShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shadowShader, "viewPos");
    const i32 lightDirLoc = GetShaderLocation(shadowShader, "lightDir");
    const i32 lightColLoc = GetShaderLocation(shadowShader, "lightColor");
    const i32 ambientLoc = GetShaderLocation(shadowShader, "ambient");
    const i32 lightVPLoc = GetShaderLocation(shadowShader, "lightVP");
    const i32 shadowMapLoc = GetShaderLocation(shadowShader, "shadowMap");

    Vector3 lightDir = Vector3Normalize((Vector3){ 0.35f, -1.0f, -0.35f });
    SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

    Color lightColor = (Color){255, 255, 255, 255};
    const Vector4 lightColorNormalized = ColorNormalize(lightColor);
    SetShaderValue(shadowShader, lightColLoc, &lightColorNormalized, SHADER_UNIFORM_VEC4);

    const f32 ambient[4] = {0.1f, 0.1f, 0.1f, 1.0f};
    SetShaderValue(shadowShader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    const i32 shadowMapResolution = SHADOWMAP_RESOLUTION;
    SetShaderValue(shadowShader, GetShaderLocation(shadowShader, "shadowMapResolution"), &shadowMapResolution, SHADER_UNIFORM_INT);

    const RenderTexture shadowMap = LoadShadowmapRenderTexture(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
    SetTextureFilter(shadowMap.depth, TEXTURE_FILTER_BILINEAR);

    Camera3D lightCam = (Camera3D){0};
    lightCam.target = Vector3Zero();
    lightCam.projection = CAMERA_ORTHOGRAPHIC;
    lightCam.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    lightCam.fovy = 200.0f;

    while (!WindowShouldClose()) {
        const f64 deltaTime = GetFrameTime();

        static i8 isInMainMenu = 1;
        if (isInMainMenu) {
            BeginDrawing();
            ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

            const f32 sw2 = GetScreenWidth() / 2.f;
            const i32 bw = 200, bh = 50;

            const i8* menuTitle = "main menu text";
            DrawText(menuTitle, sw2 - MeasureText(menuTitle, 40) / 2.f, 100, 40, BLACK);

            static i8 joinSelected = 0;
            if (!joinSelected && GuiButton((Rectangle){ sw2 - bw / 2.f, 200, bw, bh }, "Start Server")) {
                if (StartServer() == 0) {
                    return 0;
                }
            }

            if (!joinSelected && GuiButton((Rectangle){ sw2 - bw / 2.f, 300, bw, bh }, "Join Server")) {
                joinSelected = 1;
                continue;
            }

            static i8 ipEditMode = 0, portEditMode = 0;
            static i8 ipAddress[20] = "127.0.0.1", port[10] = "12345";
            if (joinSelected) {
                GuiLabel((Rectangle){sw2 - bw / 2.f, 200, bw, bh}, "Enter IP and Port");
                if (GuiTextBox((Rectangle){ sw2 - bw / 2.f, 250, bw, bh }, ipAddress, sizeof(ipAddress), ipEditMode)) {
                    ipEditMode = !ipEditMode;
                }
                if (GuiTextBox((Rectangle){ sw2 - bw / 2.f, 300, bw, bh }, port, sizeof(port), portEditMode)) {
                    portEditMode = !portEditMode;
                }

                if (GuiButton((Rectangle){ sw2 - bw / 2.f, 350, bw, bh }, "#159#Connect")) {
                    isInMainMenu = 1 == JoinServer(ipAddress, port);
                }
            }

            EndDrawing();
            continue;
        }

        ENetEvent event;
        while (enet_host_service(host, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE: {
                    switch (*(MsgType*)event.packet->data) {
                        case MSGTYPE_C_PLAYER_ID: {
                            if (-1 != localID) {
                                break;
                            }
                            const MsgPlayerID* idMsg = (MsgPlayerID*)event.packet->data;
                            const i32 id = idMsg->playerID;
                            players[id].id = localID = id;
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
                        case MSGTYPE_C_UPDATE_BODIES: {
                            const MsgUpdateBodies* updateMsg = (MsgUpdateBodies*)event.packet->data;
                            for (i32 i = 0; i < MAX_BODIES; i++) {
                                const Vector3 p = updateMsg->bodies[i].pos;
                                if (BODYTYPE_NULL == bodies[i].state.type && BODYTYPE_NULL != updateMsg->bodies[i].type) {
                                    const Vector3 s = updateMsg->bodies[i].size;
                                    switch (updateMsg->bodies[i].type) {
                                        case BODYTYPE_BOX: {
                                            bodies[i].display = LoadModelFromMesh(GenMeshCube(s.x, s.y, s.z));
                                        } break;
                                        case BODYTYPE_SPHERE: {
                                            bodies[i].display = LoadModelFromMesh(GenMeshSphere(s.x, 16, 16));
                                        } break;
                                        case BODYTYPE_NULL: break; // can't happen
                                    }
                                    bodies[i].display.materials[0].shader = shadowShader;
                                    printf("CREATED NEW MESH, SIZE: %f %f %f, POS: %f %f %f\n", s.x, s.y, s.z, p.x, p.y, p.z);
                                }

                                bodies[i].state = updateMsg->bodies[i];
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

        if (localID == -1) {
            BeginDrawing();
            ClearBackground(BLACK);
                DrawText("Waiting to receive ID from server", 100, 100, 30, RAYWHITE);
            EndDrawing();
            continue;
        }

        Player_UpdateLocal(2.f, 2.f, deltaTime);
        MsgPlayerUpdate msg = { .msg = MSGTYPE_S_PLAYER_UPDATE, .player = players[localID] };
        ENetPacket* packet = enet_packet_create(&msg, sizeof(MsgPlayerUpdate), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, 0, packet);

        const Vector3 camPos = playerCam.position;
        SetShaderValue(shadowShader, shadowShader.locs[SHADER_LOC_VECTOR_VIEW], &camPos, SHADER_UNIFORM_VEC3);

        const f32 cameraSpeed = 0.05f * 60.f * deltaTime;
        if (IsKeyDown(KEY_LEFT)  && lightDir.x <  0.6f) lightDir.x += cameraSpeed;
        if (IsKeyDown(KEY_RIGHT) && lightDir.x > -0.6f) lightDir.x -= cameraSpeed;
        if (IsKeyDown(KEY_UP)    && lightDir.z <  0.6f) lightDir.z += cameraSpeed;
        if (IsKeyDown(KEY_DOWN)  && lightDir.z > -0.6f) lightDir.z -= cameraSpeed;
        lightDir = Vector3Normalize(lightDir);
        lightCam.position = Vector3Scale(lightDir, -200.0f);
        SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

        static f32 spawnTimer = 0.f;
        spawnTimer += deltaTime;
        if (IsKeyDown(KEY_M) && spawnTimer > 0.1f) {
            spawnTimer = 0.0f;
            const Vector3 pos = {Rand_Double(-4.0, 4.0), Rand_Double(20.0, 50.0), Rand_Double(-4.0, 4.0)};
            if (Rand_Int(0, 2) == 0) {
                ClientAddBody((BodyState){
                    .type = BODYTYPE_BOX,
                    .pos = pos,
                    .rot = (Vector3){0.f, 0.f, 0.f},
                    .size = (Vector3){Rand_Double(0.2, 1.0), Rand_Double(0.2, 1.0), Rand_Double(0.2, 1.0)},
                    .col = Rand_Color(30, 190)
                });
            } else {
                ClientAddBody((BodyState){
                    .type = BODYTYPE_SPHERE,
                    .pos = pos,
                    .rot = (Vector3){0.f, 0.f, 0.f},
                    .size = (Vector3){Rand_Double(0.1, 0.4), 0.f, 0.f},
                    .col = Rand_Color(30, 190)
                });
            }
        }
        if (IsKeyReleased(KEY_SPACE)) {
            ClientAddBody((BodyState){
                .type = BODYTYPE_SPHERE,
                .pos = camPos,
                .rot = (Vector3){0.f, 0.f, 0.f},
                .size = (Vector3){0.15f, 0.f, 0.f},
                .col = Rand_Color(30, 190)
            });
            // TODO allow clients to create bodies with initial forces
            // dBodyAddForce(bodies[ball].body, player.dir.x * 10000.f, player.dir.y * 10000.f, player.dir.z * 10000.f);
        }

        Matrix lightView, lightProj;
        BeginTextureMode(shadowMap);
        ClearBackground(WHITE);
        BeginMode3D(lightCam);
            lightView = rlGetMatrixModelview();
            lightProj = rlGetMatrixProjection();
            DrawScene(bodies);
        EndMode3D();
        EndTextureMode();

        const Matrix lightViewProj = MatrixMultiply(lightView, lightProj);
        SetShaderValueMatrix(shadowShader, lightVPLoc, lightViewProj);

        rlEnableShader(shadowShader.id);
        const i32 slot = 10; // Can be anything 0 to 15, but 0 will probably be taken up
        rlActiveTextureSlot(slot);
        rlEnableTexture(shadowMap.depth.id);
        rlSetUniform(shadowMapLoc, &slot, SHADER_UNIFORM_INT, 1);

        ClearBackground(DARKGRAY);
        BeginMode3D(playerCam);
            if (IsKeyDown(KEY_X)) {
                for (i32 i = 0; i < MAX_BODIES; i++) {
                    if (BODYTYPE_NULL == bodies[i].state.type) {
                        continue;
                    }

                    const Vector3 pos = bodies[i].state.pos;
                    // const Vector3 rot = bodies[i].state.rot;

                    // TODO fix this
                    const f32 transform[16] = {
                        0.f, 0.f, 0.f, 0.f,
                        0.f, 0.f, 0.f, 0.f,
                        0.f, 0.f, 0.f, 0.f,
                        pos.x, pos.y, pos.z, 1.f
                    };
                    // const f32 transform[16] = {
                    //     rot[0], rot[4], rot[8],  0.0f,
                    //     rot[1], rot[5], rot[9],  0.0f,
                    //     rot[2], rot[6], rot[10], 0.0f,
                    //     pos[0], pos[1], pos[2],  1.0f
                    // };

                    rlPushMatrix();
                    rlMultMatrixf(transform);

                    switch (bodies[i].state.type) {
                        case BODYTYPE_NULL: break; // obviously can't happen just to make lsp happy
                        case BODYTYPE_SPHERE: {
                            const f32 radius = bodies[i].state.size.x;
                            DrawSphereWires((Vector3){0.f, 0.f, 0.f}, radius, 12, 12, MAGENTA);
                        } break;
                        case BODYTYPE_BOX: {
                            const Vector3 size = bodies[i].state.size;
                            DrawCubeWires((Vector3){0.f, 0.f, 0.f}, size.x, size.y, size.z, MAGENTA);
                        } break;
                    }

                    rlPopMatrix();
                }
            } else {
                DrawScene(bodies);
            }
            DrawSphere(lightCam.position, 1.f, lightColor);
            DrawSphereWires(lightCam.position, 1.f, 10, 10, BLACK);
        EndMode3D();
        if (IsKeyDown(KEY_Z)) {
            DrawTextureEx(shadowMap.depth, (Vector2){0, 0}, 0.f, 0.6f, WHITE);
        }
        DrawFPS(10, 10);
        EndDrawing();
    }

    UnloadShadowmapRenderTexture(shadowMap);
    CloseWindow();
    return 0;
}

static void NearCallback(void* data, dGeomID o1, dGeomID o2) {
    const i32 MAX_CONTACTS = 8;
    dContact contacts[MAX_CONTACTS];

    const i32 nc = dCollide(o1, o2, MAX_CONTACTS, &contacts[0].geom, sizeof(dContact));
    if (nc <= 0) {
        return;
    }

    for (i32 i = 0; i < nc; i++) {
        contacts[i].surface.mode = dContactBounce; // Enable bounce
        contacts[i].surface.bounce = 0.2;          // Bounce factor
        contacts[i].surface.bounce_vel = 0.1;      // Minimum velocity for bounce
        contacts[i].surface.mu = dInfinity;        // Friction coefficient

        // Create a contact joint to handle the collision
        dJointID c = dJointCreateContact(world, contactGroup, &contacts[i]);
        dJointAttach(c, dGeomGetBody(o1), dGeomGetBody(o2));
    }
}

static inline void GetRotationMatrix(dReal res[16], Vector3 rot) {
    const dReal cx = cos(rot.x);
    const dReal sx = sin(rot.x);
    const dReal cy = cos(rot.y);
    const dReal sy = sin(rot.y);
    const dReal cz = cos(rot.z);
    const dReal sz = sin(rot.z);

    res[0] = cy * cz;
    res[1] = cz * sx * sy - cx * sz;
    res[2] = cx * cz * sy + sx * sz;
    res[3] = 0;

    res[4] = cy * sz;
    res[5] = cx * cz + sx * sy * sz;
    res[6] = -cz * sx + cx * sy * sx;
    res[7] = 0;

    res[8] = -sy;
    res[9] = cy * sx;
    res[10] = cx * cy;
    res[11] = 0;

    res[12] = 0;
    res[13] = 0;
    res[14] = 0;
    res[15] = 1;
}

static i32 AddBody(Body* bodies, BodyState* states, CollMask category, CollMask collide, BodyState state, i8 isKinematic) {
    for (i32 i = 0; i < MAX_BODIES; i++) {
        if (bodies[i].type != BODYTYPE_NULL) {
            continue;
        }


        Body* body = &bodies[i];
        body->type = state.type;
        body->body = dBodyCreate(world);
        dBodySetPosition(body->body, state.pos.x, state.pos.y, state.pos.z);

        dReal rm[16];
        GetRotationMatrix(rm, state.rot);
        dBodySetRotation(body->body, rm);

        if (isKinematic) {
            dBodySetKinematic(body->body);
        }

        switch (state.type) {
            case BODYTYPE_SPHERE: {
                body->geom = dCreateSphere(space, state.size.x);
                // body->display = LoadModelFromMesh(GenMeshSphere(state.size.x, 18, 18));
            } break;
            case BODYTYPE_BOX: {
                body->geom = dCreateBox(space, state.size.x, state.size.y, state.size.z);
                // body->display = LoadModelFromMesh(GenMeshCube(state.size.x, state.size.y, state.size.z));
            } break;
            default: return -1;
        }
        // body->display.materials[0].shader = shadowShader;
        dGeomSetCategoryBits(body->geom, category);
        dGeomSetCollideBits(body->geom, collide);
        dGeomSetBody(body->geom, body->body);

        states[i] = state;
        return i;
    }

    return -1;
}

static i32 AddBodyMap(Body* bodies, BodyState* states, Vector3 pos, Vector3 rot, Vector3 size, Color col) {
    for (i32 i = 0; i < MAX_BODIES; i++) {
        if (bodies[i].type != BODYTYPE_NULL) {
            continue;
        }

        Body* body = &bodies[i];
        body->type = BODYTYPE_BOX;
        body->geom = dCreateBox(space, size.x, size.y, size.z);

        dReal rm[16];
        GetRotationMatrix(rm, rot);
        dGeomSetRotation(body->geom, rm);
        dGeomSetPosition(body->geom, pos.x, pos.y, pos.z);

        dGeomSetCategoryBits(body->geom, CMASK_MAP);
        dGeomSetCategoryBits(body->geom, CMASK_ALL & ~CMASK_MAP);
        body->body = NULL;

        states[i] = (BodyState){ .pos = pos, .rot = rot, .size = size, .col = col, .type = BODYTYPE_BOX };
        return i;
    }

    return -1;
}

static void ReleaseBody(RenderBody* bodies, i32 id) {
    if (bodies[id].state.type == BODYTYPE_NULL) {
        return;
    }

    bodies[id].state.type = BODYTYPE_NULL;
    UnloadModel(bodies[id].display);
}

static void ClientAddBody(BodyState body) {
    MsgNewBody msg = { .msg = MSGTYPE_S_NEW_BODY, .body = body };
    ENetPacket* packet = enet_packet_create(&msg, sizeof(MsgNewBody), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, packet);
}

static RenderTexture LoadShadowmapRenderTexture(i32 width, i32 height) {
    RenderTexture target = { 0 };

    target.id = rlLoadFramebuffer(); // Load an empty framebuffer
    target.texture.width = width;
    target.texture.height = height;

    if (target.id > 0) {
        rlEnableFramebuffer(target.id);

        // Create depth texture
        // We don't need a color texture for the shadowmap
        target.depth.id = rlLoadTextureDepth(width, height, false);
        target.depth.width = width;
        target.depth.height = height;
        target.depth.format = 19;       //DEPTH_COMPONENT_24BIT?
        target.depth.mipmaps = 1;

        // Attach depth texture to FBO
        rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

        // Check if fbo is complete with attachments (valid)
        if (rlFramebufferComplete(target.id)) TRACELOG(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully", target.id);

        rlDisableFramebuffer();
    } else {
        TRACELOG(LOG_WARNING, "FBO: Framebuffer object can not be created");
    }

    return target;
}

// Unload shadowmap render texture from GPU memory (VRAM)
static void UnloadShadowmapRenderTexture(RenderTexture2D target) {
    if (target.id > 0) {
        // NOTE: Depth texture/renderbuffer is automatically
        // queried and deleted before deleting framebuffer
        rlUnloadFramebuffer(target.id);
    }
}
