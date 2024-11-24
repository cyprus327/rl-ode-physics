#include <ode/objects.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "ode/ode.h"

#include "../inc/util.h" // shouldn't have to do this but zed sucks for some reason

#define MAX_PITCH (89.f * DEG2RAD)
#define MAX_BODIES 512

#define SHADOWMAP_RESOLUTION 1024

static Camera cam = { .position = {0.f, 2.f, -3.f}, .fovy = 90.f, .projection = CAMERA_PERSPECTIVE, .up = {0.f, 1.f, 0.f} };

typedef enum bodyType {
    BODYTYPE_SPHERE,
    BODYTYPE_BOX,
    BODYTYPE_TRIMESH
} BodyType;

typedef struct body {
    dBodyID body;
    dGeomID geom;
    BodyType type;
    Model display;
    Vector3 size;
    Color color;
} Body;

static Body bodies[MAX_BODIES];
static i32 bodiesCount;

static dWorldID world;
static dSpaceID space;
static dJointGroupID contactGroup;

static Model floorModel;

static u32 randState;

static u32 Rand_Next(void);
static i32 Rand_Int(i32 min, i32 max);
static f64 Rand_Double(f64 min, f64 max);

static void HandleInput(Camera3D* camera, f32 moveSpeed, f32 turnSpeed, f32 dt);
static void NearCallback(void* data, dGeomID o1, dGeomID o2);
static i32 AddBody(BodyType type, Vector3 pos, Vector3 size, i8 isKinematic);
static i32 AddBodyTris(Model model, Vector3 pos, Vector3 size, i8 isKinematic);

// all shadowmap stuff copied from the raylib example shadowmap project
static RenderTexture LoadShadowmapRenderTexture(i32 width, i32 height);
static void UnloadShadowmapRenderTexture(RenderTexture2D target);

static void DrawScene(void) {
    DrawModel(floorModel, (Vector3){0.f, -0.5f, 0.f}, 1.f, WHITE);
    for (i32 i = 0; i < bodiesCount; i++) {
        const dReal* pos = dBodyGetPosition(bodies[i].body);
        const dReal* rot = dBodyGetRotation(bodies[i].body);
        bodies[i].display.transform = (Matrix){
            rot[0], rot[1], rot[2], pos[0],
            rot[4], rot[5], rot[6], pos[1],
            rot[8], rot[9], rot[10], pos[2],
            0.f, 0.f, 0.f, 1.f
        };

        DrawModel(bodies[i].display, (Vector3){0.f, 0.f, 0.f}, 1.f, bodies[i].color);
    }
}

i32 main(void) {
    SetTargetFPS(1000);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Window");

    dInitODE();
    world = dWorldCreate();
    dWorldSetGravity(world, 0.0, -9.8, 0.0);
    space = dHashSpaceCreate(0);
    contactGroup = dJointGroupCreate(0);

    randState = (u32)time(NULL);

    const Shader shadowShader = LoadShader("res/shadowMap.vert", "res/shadowMap.frag");
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
    // Use an orthographic projection for directional lights
    lightCam.projection = CAMERA_ORTHOGRAPHIC;
    lightCam.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    lightCam.fovy = 90.0f;

    const Texture texture = LoadTexture("res/grassTexture.png");
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);

    // works just messed up collisions
    // const i32 body0 = AddBodyTris(LoadModel("res/grassPlane.obj"), (Vector3){0.f, 0.f, 0.f}, (Vector3){0.1f, 0.1f, 0.1f}, 1);
    // bodies[body0].display.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;

    const dGeomID floorID = dCreatePlane(space, 0, 1, 0, 0);
    floorModel = LoadModelFromMesh(GenMeshCube(100.f, 1.f, 100.f));
    floorModel.materials[0].shader = shadowShader;
    floorModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;

    while (!WindowShouldClose()) {
        const f64 deltaTime = GetFrameTime();
        HandleInput(&cam, 2.f, 2.f, deltaTime);

        const Vector3 camPos = cam.position;
        SetShaderValue(shadowShader, shadowShader.locs[SHADER_LOC_VECTOR_VIEW], &camPos, SHADER_UNIFORM_VEC3);

        const f32 cameraSpeed = 0.05f;
        if (IsKeyDown(KEY_LEFT) && lightDir.x < 0.6f) {
            lightDir.x += cameraSpeed * 60.0f * deltaTime;
        }
        if (IsKeyDown(KEY_RIGHT) && lightDir.x > -0.6f) {
            lightDir.x -= cameraSpeed * 60.0f * deltaTime;
        }
        if (IsKeyDown(KEY_UP) && lightDir.z < 0.6f) {
            lightDir.z += cameraSpeed * 60.0f * deltaTime;
        }
        if (IsKeyDown(KEY_DOWN) && lightDir.z > -0.6f) {
            lightDir.z -= cameraSpeed * 60.0f * deltaTime;
        }
        if (IsKeyDown(KEY_N)) {
            lightCam.fovy -= 30.f * deltaTime;
            printf("\tFOV: %f\n", lightCam.fovy);
        }
        if (IsKeyDown(KEY_M)) {
            lightCam.fovy += 30.f * deltaTime;
            printf("\tFOV: %f\n", lightCam.fovy);
        }
        lightDir = Vector3Normalize(lightDir);
        lightCam.position = Vector3Scale(lightDir, -20.0f);
        SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

        static f32 spawnTimer = 0.f;
        spawnTimer += deltaTime;
        if (spawnTimer > 0.05f && bodiesCount < MAX_BODIES / 2) {
            spawnTimer = 0.0f;

            const Vector3 pos = {Rand_Double(-4.0, 4.0), Rand_Double(20.0, 50.0), Rand_Double(-4.0, 4.0)};
            i32 added;
            switch (Rand_Int(0, 2)) {
                case BODYTYPE_BOX: {
                    added = AddBody(BODYTYPE_BOX, pos, (Vector3){Rand_Double(0.2, 1.0), Rand_Double(0.2, 1.0), Rand_Double(0.2, 1.0)}, 0);
                } break;
                case BODYTYPE_SPHERE: {
                    added = AddBody(BODYTYPE_SPHERE, pos, (Vector3){Rand_Double(0.1, 0.4), 0.f, 0.f}, 0);
                } break;
            }
            bodies[added].display.materials[0].shader = shadowShader;
        }

        if (IsKeyReleased(KEY_SPACE) && bodiesCount < MAX_BODIES) {
            const Vector3 d = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
            const i32 ball = AddBody(BODYTYPE_SPHERE, camPos, (Vector3){0.15f, 0.f, 0.f}, 0);
            dBodyAddForce(bodies[ball].body, d.x * 10000.f, d.y * 10000.f, d.z * 10000.f);
            printf("D: %f %f %f\n", d.x, d.y, d.z);
        }

        dSpaceCollide(space, NULL, NearCallback);
        dWorldStep(world, deltaTime);
        dJointGroupEmpty(contactGroup);

        Matrix lightView, lightProj;
        BeginTextureMode(shadowMap);
        ClearBackground(WHITE);
        BeginMode3D(lightCam);
            lightView = rlGetMatrixModelview();
            lightProj = rlGetMatrixProjection();
            DrawScene();
        EndMode3D();
        EndTextureMode();

        ClearBackground(DARKGRAY);

        const Matrix lightViewProj = MatrixMultiply(lightView, lightProj);
        SetShaderValueMatrix(shadowShader, lightVPLoc, lightViewProj);

        rlEnableShader(shadowShader.id);
        const i32 slot = 10; // Can be anything 0 to 15, but 0 will probably be taken up
        rlActiveTextureSlot(slot);
        rlEnableTexture(shadowMap.depth.id);
        rlSetUniform(shadowMapLoc, &slot, SHADER_UNIFORM_INT, 1);

        BeginMode3D(cam);
            if (IsKeyDown(KEY_X)) {
                DrawModel(floorModel, (Vector3){0.f, -0.5f, 0.f}, 1.f, WHITE);
                for (i32 i = 0; i < bodiesCount; i++) {
                    const dReal* pos = dBodyGetPosition(bodies[i].body);
                    const dReal* rot = dBodyGetRotation(bodies[i].body);
                    const f32 transform[16] = {
                        rot[0], rot[4], rot[8],  0.0f,
                        rot[1], rot[5], rot[9],  0.0f,
                        rot[2], rot[6], rot[10], 0.0f,
                        pos[0], pos[1], pos[2],  1.0f
                    };

                    rlPushMatrix();
                    rlMultMatrixf(transform);

                    switch (bodies[i].type) {
                        case BODYTYPE_SPHERE: {
                            const f32 radius = dGeomSphereGetRadius(bodies[i].geom);
                            DrawSphereWires((Vector3){0.f, 0.f, 0.f}, radius, 12, 12, MAGENTA);
                        } break;
                        case BODYTYPE_BOX: {
                            dVector3 sides;
                            dGeomBoxGetLengths(bodies[i].geom, sides);
                            DrawCubeWires((Vector3){0.f, 0.f, 0.f}, sides[0], sides[1], sides[2], MAGENTA);
                        } break;
                        case BODYTYPE_TRIMESH: {
                            DrawModelWires(bodies[i].display, (Vector3){0.f, 0.f, 0.f}, 1.f, MAGENTA);
                        } break;
                    }

                    rlPopMatrix();
                }
            } else {
                DrawScene();
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

    for (i32 i = 0; i < bodiesCount; i++) {
        dBodyDestroy(bodies[i].body);
        dGeomDestroy(bodies[i].geom);
        UnloadModel(bodies[i].display);
    }
    dGeomDestroy(floorID);
    UnloadModel(floorModel);
    dJointGroupDestroy(contactGroup);
    dWorldDestroy(world);
    dCloseODE();
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

static i32 AddBody(BodyType type, Vector3 pos, Vector3 size, i8 isKinematic) {
    if (bodiesCount >= MAX_BODIES) {
        return -1;
    }

    Body* body = &bodies[bodiesCount];
    body->color = (Color){Rand_Int(70, 190), Rand_Int(70, 190), Rand_Int(70, 190), 255};
    body->type = type;
    body->size = size;
    switch (type) {
        case BODYTYPE_SPHERE: {
            body->geom = dCreateSphere(space, body->size.x);
            body->display = LoadModelFromMesh(GenMeshSphere(body->size.x, 10, 10));
        } break;
        case BODYTYPE_BOX: {
            body->geom = dCreateBox(space, body->size.x, body->size.y, body->size.z);
            body->display = LoadModelFromMesh(GenMeshCube(body->size.x, body->size.y, body->size.z));
        } break;
        default: return -1;
    }
    body->body = dBodyCreate(world);

    dGeomSetBody(body->geom, body->body);

    dBodySetPosition(body->body, pos.x, pos.y, pos.z);

    if (isKinematic) {
        dBodySetKinematic(bodies[bodiesCount].body);
    }

    return bodiesCount++;
}

// use when stuff can be destroyed/freed, ode is better understood
static i32 AddBodyTris(Model model, Vector3 pos, Vector3 size, i8 isKinematic) {
    if (bodiesCount >= MAX_BODIES) {
        return -1;
    }

    Body* body = &bodies[bodiesCount];
    body->color = WHITE;
    body->type = BODYTYPE_TRIMESH,
    body->size = size;
    body->display = model;
    body->body = dBodyCreate(world);

    const i32 vertCount = model.meshes[0].vertexCount;
    i32* groundInd = malloc(vertCount * sizeof(i32));
    for (i32 i = 0; i < vertCount; i++) {
        groundInd[i] = i;
    }

    const dTriMeshDataID triData = dGeomTriMeshDataCreate();
    dGeomTriMeshDataBuildSingle(triData, model.meshes[0].vertices,
                                3 * sizeof(f32), vertCount,
                                groundInd, vertCount,
                                3 * sizeof(i32));
    body->geom = dCreateTriMesh(space, triData, NULL, NULL, NULL);

    dGeomSetBody(body->geom, body->body);

    dBodySetPosition(body->body, pos.x, pos.y, pos.z);

    if (isKinematic) {
        dBodySetKinematic(bodies[bodiesCount].body);
    }

    return bodiesCount++;
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
    if (target.id > 0)
    {
        // NOTE: Depth texture/renderbuffer is automatically
        // queried and deleted before deleting framebuffer
        rlUnloadFramebuffer(target.id);
    }
}

static void HandleInput(Camera3D* camera, f32 moveSpeed, f32 turnSpeed, f32 dt) {
    static float mult = 1.f;
    if (IsKeyDown(KEY_LEFT_SHIFT)) {
        mult += dt;
        moveSpeed += mult * 10.f;
    } else {
        mult = 1.f;
    }

    static f32 yaw = 0.0f;
    static f32 pitch = 0.0f;

    Vector3 movement = {0.f, 0.f, 0.f};
    if (IsKeyDown(KEY_W)) movement.z += moveSpeed * dt;
    if (IsKeyDown(KEY_S)) movement.z -= moveSpeed * dt;
    if (IsKeyDown(KEY_A)) movement.x += moveSpeed * dt;
    if (IsKeyDown(KEY_D)) movement.x -= moveSpeed * dt;
    if (IsKeyDown(KEY_Q)) movement.y -= moveSpeed * dt;
    if (IsKeyDown(KEY_E)) movement.y += moveSpeed * dt;

    Vector3 rotation;
    if (IsKeyDown(KEY_I)) pitch += turnSpeed * dt;
    if (IsKeyDown(KEY_K)) pitch -= turnSpeed * dt;
    if (IsKeyDown(KEY_J)) yaw += turnSpeed * dt;
    if (IsKeyDown(KEY_L)) yaw -= turnSpeed * dt;
    pitch = Clamp(pitch, -MAX_PITCH, MAX_PITCH);
    camera->fovy = IsKeyDown(KEY_F) ? 40.f : 90.f;

    const Vector3 forward = Vector3Normalize((Vector3){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw)
    });

    const Vector3 right = Vector3Normalize(
        Vector3CrossProduct(camera->up, forward));

    camera->position = Vector3Add(camera->position, Vector3Scale(forward, movement.z));
    camera->position = Vector3Add(camera->position, Vector3Scale(right, movement.x));
    camera->position.y += movement.y;

    camera->target = Vector3Add(camera->position, forward);
}

u32 Rand_Next(void) {
    randState += 0xE120FC15;
    unsigned long long temp = (unsigned long long)randState * 0x4A39B70D;
    const u32 m1 = (u32)((temp >> 32) ^ temp);
    temp = (unsigned long long)m1 * 0x12FAD5C9;
    return (u32)((temp >> 32) ^ temp);
}

i32 Rand_Int(i32 min, i32 max) {
    if (min >= max) {
        printf("Min >= Max (%d, %d)\n", min, max);
        return 0;
    }

    return (i32)(Rand_Next() % (max - min)) + min;
}

f64 Rand_Double(f64 min, f64 max) {
    if (min >= max) {
        printf("Min >= Max (%f, %f)\n", min, max);
    }

    return (f64)(min + Rand_Next() / (f64)0xFFFFFFFF * ((f64)max - (f64)min));
}
