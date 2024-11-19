#include <ode/contact.h>
#include <ode/objects.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "ode/ode.h"

#define MAX_PITCH (89.f * DEG2RAD)
#define MAX_SHAPES 512

#define SHADOWMAP_RESOLUTION 1024

static Camera cam = { .position = {0.f, 2.f, -3.f}, .fovy = 90.f, .projection = CAMERA_PERSPECTIVE, .up = {0.f, 1.f, 0.f} };

typedef enum bodyType {
    BODYTYPE_SPHERE,
    BODYTYPE_BOX
} BodyType;

typedef struct body {
    dBodyID body;
    dGeomID geom;
    BodyType type;
    Model display;
    Vector3 size;
    Color color;
} Body;

static Body bodies[MAX_SHAPES];
static int bodiesCount;

static dWorldID world;
static dSpaceID space;
static dJointGroupID contactGroup;

static Model floorModel;

static unsigned int randState;

static unsigned int Rand_Next(void);
static int Rand_Int(int min, int max);
static double Rand_Double(double min, double max);

static void HandleInput(Camera3D* camera, float moveSpeed, float turnSpeed, float dt);
static int AddBody(BodyType type, Vector3 pos, Vector3 size, char isKinematic);
static void NearCallback(void* data, dGeomID o1, dGeomID o2);

static RenderTexture LoadShadowmapRenderTexture(int width, int height);
static void UnloadShadowmapRenderTexture(RenderTexture2D target);

static void DrawScene(void) {
    DrawModel(floorModel, (Vector3){0.f, -0.5f, 0.f}, 1.f, WHITE);
    for (int i = 0; i < bodiesCount; i++) {
        const dReal* pos = dBodyGetPosition(bodies[i].body);
        const dReal* rot = dBodyGetRotation(bodies[i].body);
        const float transform[16] = { // ode returns column major
            rot[0], rot[4], rot[8], 0.0f,
            rot[1], rot[5], rot[9], 0.0f,
            rot[2], rot[6], rot[10], 0.0f,
            pos[0], pos[1], pos[2], 1.0f
        };

        rlPushMatrix();
        rlMultMatrixf(transform);

        // memcpy(&bodies[i].display.transform, transform, sizeof(transform));
        DrawModel(bodies[i].display, Vector3Zero(), 1.f, bodies[i].color);

        rlPopMatrix();
    }
}

int main(void) {
    SetTargetFPS(1000);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Window");

    dInitODE();
    world = dWorldCreate();
    dWorldSetGravity(world, 0.0, -9.8, 0.0);
    space = dHashSpaceCreate(0);
    contactGroup = dJointGroupCreate(0);

    randState = (unsigned int)time(NULL);

    const Texture texture = LoadTexture("res/grassTexture.png");

    const Shader shadowShader = LoadShader("res/shadowMap.vert", "res/shadowMap.frag");
    shadowShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shadowShader, "viewPos");
    const int lightDirLoc = GetShaderLocation(shadowShader, "lightDir");
    const int lightColLoc = GetShaderLocation(shadowShader, "lightColor");
    const int ambientLoc = GetShaderLocation(shadowShader, "ambient");
    const int lightVPLoc = GetShaderLocation(shadowShader, "lightVP");
    const int shadowMapLoc = GetShaderLocation(shadowShader, "shadowMap");

    Vector3 lightDir = Vector3Normalize((Vector3){ 0.35f, -1.0f, -0.35f });
    SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

    Color lightColor = WHITE;
    const Vector4 lightColorNormalized = ColorNormalize(lightColor);
    SetShaderValue(shadowShader, lightColLoc, &lightColorNormalized, SHADER_UNIFORM_VEC4);

    const float ambient[4] = {0.1f, 0.1f, 0.1f, 1.0f};
    SetShaderValue(shadowShader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    const int shadowMapResolution = SHADOWMAP_RESOLUTION;
    SetShaderValue(shadowShader, GetShaderLocation(shadowShader, "shadowMapResolution"), &shadowMapResolution, SHADER_UNIFORM_INT);

    const RenderTexture shadowMap = LoadShadowmapRenderTexture(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
    Camera3D lightCam = (Camera3D){0};
    lightCam.target = Vector3Zero();
    // Use an orthographic projection for directional lights
    lightCam.projection = CAMERA_ORTHOGRAPHIC;
    lightCam.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    lightCam.fovy = 30.0f;

    const dGeomID floorID = dCreatePlane(space, 0, 1, 0, 0);
    floorModel = LoadModelFromMesh(GenMeshCube(50.f, 1.f, 50.f));
    floorModel.materials[0].shader = shadowShader;
    floorModel.materials[0].maps[0].texture = texture;

    while (!WindowShouldClose()) {
        const double deltaTime = GetFrameTime();
        HandleInput(&cam, 2.f, 2.f, deltaTime);

        const Vector3 camPos = cam.position;
        SetShaderValue(shadowShader, shadowShader.locs[SHADER_LOC_VECTOR_VIEW], &camPos, SHADER_UNIFORM_VEC3);

        const float cameraSpeed = 0.05f;
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
        lightDir = Vector3Normalize(lightDir);
        lightCam.position = Vector3Scale(lightDir, -15.0f);
        SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

        static float spawnTimer = 0.f;
        spawnTimer += deltaTime;
        if (spawnTimer > 0.05f && bodiesCount < MAX_SHAPES) {
            spawnTimer = 0.0f;

            const Vector3 pos = {Rand_Double(-6.0, 6.0), Rand_Double(20.0, 50.0), Rand_Double(-6.0, 6.0)};
            int added;
            switch ((BodyType)Rand_Int(0, 2)) {
                case BODYTYPE_BOX: {
                    added = AddBody(BODYTYPE_BOX, pos, (Vector3){Rand_Double(0.2, 1.0), Rand_Double(0.2, 1.0), Rand_Double(0.2, 1.0)}, 0);
                } break;
                case BODYTYPE_SPHERE: {
                    added = AddBody(BODYTYPE_SPHERE, pos, (Vector3){Rand_Double(0.1, 0.4), 0.f, 0.f}, 0);
                } break;
            }
            bodies[added].display.materials[0].shader = shadowShader;
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

        ClearBackground(RAYWHITE);

        const Matrix lightViewProj = MatrixMultiply(lightView, lightProj);
        SetShaderValueMatrix(shadowShader, lightVPLoc, lightViewProj);

        rlEnableShader(shadowShader.id);
        const int slot = 10; // Can be anything 0 to 15, but 0 will probably be taken up
        rlActiveTextureSlot(slot);
        rlEnableTexture(shadowMap.depth.id);
        rlSetUniform(shadowMapLoc, &slot, SHADER_UNIFORM_INT, 1);

        BeginMode3D(cam);
            if (IsKeyDown(KEY_X)) {
                DrawModel(floorModel, (Vector3){0.f, -0.5f, 0.f}, 1.f, WHITE);
                for (int i = 0; i < bodiesCount; i++) {
                    const dReal* pos = dBodyGetPosition(bodies[i].body);
                    const dReal* rot = dBodyGetRotation(bodies[i].body);
                    const float transform[16] = { // ode returns column major
                        rot[0], rot[4], rot[8], 0.0f,
                        rot[1], rot[5], rot[9], 0.0f,
                        rot[2], rot[6], rot[10], 0.0f,
                        pos[0], pos[1], pos[2], 1.0f
                    };

                    rlPushMatrix();
                    rlMultMatrixf(transform);

                    switch (bodies[i].type) {
                        case BODYTYPE_SPHERE: {
                            const float radius = dGeomSphereGetRadius(bodies[i].geom);
                            DrawSphereWires(Vector3Zero(), radius, 12, 12, MAGENTA);
                        } break;
                        case BODYTYPE_BOX: {
                            dVector3 sides;
                            dGeomBoxGetLengths(bodies[i].geom, sides);
                            DrawCubeWires(Vector3Zero(), sides[0], sides[1], sides[2], MAGENTA);
                        } break;
                    }

                    rlPopMatrix();
                }
            } else {
                DrawScene();
            }
            DrawSphere(lightCam.position, 1.f, BLUE);
        EndMode3D();
        if (IsKeyDown(KEY_Z)) {
            DrawTextureEx(shadowMap.depth, (Vector2){0, 0}, 0.f, 0.6f, WHITE);
        }
        DrawFPS(10, 10);
        EndDrawing();
    }

    for (int i = 0; i < bodiesCount; i++) {
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

static int AddBody(BodyType type, Vector3 pos, Vector3 size, char isKinematic) {
    if (bodiesCount >= MAX_SHAPES) {
        return -1;
    }

    Body* shape = &bodies[bodiesCount];
    shape->color = (Color){Rand_Int(70, 190), Rand_Int(70, 190), Rand_Int(70, 190), 255};
    shape->type = type;
    shape->size = size;
    shape->body = dBodyCreate(world);
    switch (type) {
        case BODYTYPE_SPHERE: {
            shape->geom = dCreateSphere(space, shape->size.x);
            shape->display = LoadModelFromMesh(GenMeshSphere(shape->size.x, 10, 10));
        } break;
        case BODYTYPE_BOX: {
            shape->geom = dCreateBox(space, shape->size.x, shape->size.y, shape->size.z);
            shape->display = LoadModelFromMesh(GenMeshCube(shape->size.x, shape->size.y, shape->size.z));
        } break;
    }

    dGeomSetBody(shape->geom, shape->body);

    dBodySetPosition(shape->body, pos.x, pos.y, pos.z);

    if (isKinematic) {
        dBodySetKinematic(bodies[bodiesCount].body);
    }

    return bodiesCount++;
}

static void NearCallback(void* data, dGeomID o1, dGeomID o2) {
    // Maximum number of contact points
    const int MAX_CONTACTS = 8;
    dContact contacts[MAX_CONTACTS];

    // Check for collisions between o1 and o2
    const int nc = dCollide(o1, o2, MAX_CONTACTS, &contacts[0].geom, sizeof(dContact));
    if (nc <= 0) {
        return;
    }

    for (int i = 0; i < nc; i++) {
        contacts[i].surface.mode = dContactBounce; // Enable bounce
        contacts[i].surface.bounce = 0.2;         // Bounce factor
        contacts[i].surface.bounce_vel = 0.1;     // Minimum velocity for bounce
        contacts[i].surface.mu = dInfinity;       // Friction coefficient

        // Create a contact joint to handle the collision
        dJointID c = dJointCreateContact(world, contactGroup, &contacts[i]);
        dJointAttach(c, dGeomGetBody(o1), dGeomGetBody(o2));
    }
}

RenderTexture LoadShadowmapRenderTexture(int width, int height) {
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
void UnloadShadowmapRenderTexture(RenderTexture2D target) {
    if (target.id > 0)
    {
        // NOTE: Depth texture/renderbuffer is automatically
        // queried and deleted before deleting framebuffer
        rlUnloadFramebuffer(target.id);
    }
}

static void HandleInput(Camera3D* camera, float moveSpeed, float turnSpeed, float dt) {
    static float yaw = 0.0f;
    static float pitch = 0.0f;

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

unsigned int Rand_Next(void) {
    randState += 0xE120FC15;
    unsigned long long temp = (unsigned long long)randState * 0x4A39B70D;
    const unsigned int m1 = (unsigned int)((temp >> 32) ^ temp);
    temp = (unsigned long long)m1 * 0x12FAD5C9;
    return (unsigned int)((temp >> 32) ^ temp);
}

int Rand_Int(int min, int max) {
    if (min >= max) {
        printf("Min >= Max (%d, %d)\n", min, max);
        return 0;
    }

    return (int)(Rand_Next() % (max - min)) + min;
}

double Rand_Double(double min, double max) {
    if (min >= max) {
        printf("Min >= Max (%f, %f)\n", min, max);
    }

    return (double)(min + Rand_Next() / (double)0xFFFFFFFF * ((double)max - (double)min));
}
