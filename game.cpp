#include "dais.h"
#include "dais_render.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "arena.cpp"
#include "animation.cpp"

struct point {
    u32 X;
    u32 Y;
    f32 R;
    f32 G;
    f32 B;
};

struct state {
    u64 LastPrintTime;
    f32 RedValue;
    u32 TempArenaMaxSize;
    memory_arena TempArena;
    memory_arena GameArena;
    dais_file SkeletonFile;
    skinned_mesh *SkinnedMesh;

    // use a pointer so we can hotswap a size change
    shader_state *ShaderState;
};

#define TEMP_MEM_SIZE 4096*16
#define GAME_OFFSET 4096

extern "C"
DAIS_UPDATE_AND_RENDER(GameUpdate) {
    state *State = (state *) Platform->Memory;
    if (!Platform->Initialized) {
        State->LastPrintTime = Input->SystemTimeMS;
        ArenaInit(&State->TempArena, Platform->Memory + (Platform->MemorySize - TEMP_MEM_SIZE), TEMP_MEM_SIZE);
        ArenaInit(&State->GameArena, Platform->Memory + GAME_OFFSET, Platform->MemorySize - TEMP_MEM_SIZE - 2*GAME_OFFSET);
        State->RedValue = 1.0f;
        Platform->Initialized = true;

        State->SkeletonFile = Platform->MapReadOnlyFile("../DefaultAvatar.skm");
        if (State->SkeletonFile.Handle == DAIS_BAD_FILE) {
            printf("Failed to load default avatar.\n");
            exit(-1);
        } else {
            printf("Loaded default avatar, %u bytes at %p.\n", State->SkeletonFile.Size, State->SkeletonFile.Data);
            State->SkinnedMesh = LoadMeshData(&State->GameArena, State->SkeletonFile.Data);
            UploadMeshesToOGL(State->SkinnedMesh);

        }
    }

    if (Platform->JustReloaded) {
        State->ShaderState = InitShaders(&State->GameArena);
    }

    if (Input->UnassignedButton0.Pressed) {
        State->RedValue += 1.5f * Input->FrameDeltaSec;
    }
    if (Input->UnassignedButton3.Pressed) {
        State->RedValue -= 1.5f * Input->FrameDeltaSec;
    }
    if (State->RedValue < 0) State->RedValue = 0;
    if (State->RedValue > 1) State->RedValue = 1;

    f32 g = (f32) Input->CursorX / Input->WindowWidth;
    f32 b = (f32) Input->CursorY / Input->WindowHeight;
    glClearColor(0.0f, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    float aspect = (float) Input->WindowWidth / Input->WindowHeight;
    float zoom = 100;
    glm::mat4 Projection = glm::ortho(
        -zoom * aspect, zoom * aspect, // left, right
        0.0f, zoom * 2, // bottom, top
        -100.0f * zoom, 100.0f * zoom // near, far
    );

    float Yaw = (g - 0.5f) * 360;
    float Pitch = (b - 0.5f) * 180;
    glm::vec3 LookDirection = glm::vec3(0, 0, 1);
    LookDirection = glm::rotateX(LookDirection, Pitch);
    LookDirection = glm::rotateY(LookDirection, Yaw);

    glm::mat4 Rotation = glm::lookAt(
        glm::vec3(0.0f),
        LookDirection,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    glm::mat4 Combined = Projection * Rotation;

    RenderSkinnedMesh(State->ShaderState, State->SkinnedMesh, Combined);

    if (State->TempArena.Pos > State->TempArenaMaxSize) {
        State->TempArenaMaxSize = State->TempArena.Pos;
        printf("New TempArena Max Size: %u\n", State->TempArena.Pos);
    }
    ArenaClear(&State->TempArena);
}

