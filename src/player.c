#include "../inc/player.h"

#define MAX_PITCH (89.f * DEG2RAD)

PlayerState players[MAX_PLAYERS];
i32 localID = -1;

Camera playerCam = { .position = {0.f, 2.f, -3.f}, .fovy = 90.f, .projection = CAMERA_PERSPECTIVE, .up = {0.f, 1.f, 0.f} };

void Player_UpdateLocal(f32 moveSpeed, f32 turnSpeed, f32 dt) {
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
    playerCam.fovy = IsKeyDown(KEY_F) ? 40.f : 90.f;

    const Vector3 forward = Vector3Normalize((Vector3){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw)
    });

    const Vector3 right = Vector3Normalize(Vector3CrossProduct(playerCam.up, forward));

    playerCam.position = Vector3Add(playerCam.position, Vector3Scale(forward, movement.z));
    playerCam.position = Vector3Add(playerCam.position, Vector3Scale(right, movement.x));
    playerCam.position.y += movement.y;

    playerCam.target = Vector3Add(playerCam.position, forward);

    players[localID].pos = playerCam.position;
    players[localID].dir = forward;
}
