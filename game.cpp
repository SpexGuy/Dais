// -------- Library Includes ---------

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb/stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/quaternion.hpp>

using glm::vec2;
using glm::vec3;
using glm::quat;
using glm::mat4;


// -------- Dais Includes --------

#include "dais.h"
#include "dais_render.h"
#include "arena.cpp"


// -------- Global Variables --------

memory_arena *TempArena;
memory_arena *PermArena;


// -------- Source Files --------

#include "animation.cpp"

struct state {
    u32 TempArenaMaxSize;
    memory_arena TempArena;
    memory_arena GameArena;
    dais_file SkeletonFile;

    skinned_mesh *SkinnedMesh;
    normal_mesh *Normals;
    // use a pointer so we can hotswap a size change
    shader_state *ShaderState;

    vec2 LastCursorPos;
    vec2 CamPos;
};

#define TEMP_MEM_SIZE Megabytes(2)
#define GAME_OFFSET Kilobytes(4)

extern "C"
DAIS_UPDATE_AND_RENDER(GameUpdate) {
    Assert(GAME_OFFSET > sizeof(state));

    // ---------- Initialization ----------
    state *State = (state *) Platform->Memory;
    TempArena = &State->TempArena;
    PermArena = &State->GameArena;
    if (!Platform->Initialized) {
        ArenaInit(&State->TempArena, Platform->Memory + (Platform->MemorySize - TEMP_MEM_SIZE), TEMP_MEM_SIZE);
        ArenaInit(&State->GameArena, Platform->Memory + GAME_OFFSET, Platform->MemorySize - TEMP_MEM_SIZE - 2*GAME_OFFSET);
        Platform->Initialized = true;

        State->SkeletonFile = Platform->MapReadOnlyFile("../Avatar/DefaultAvatar.skm");
        if (State->SkeletonFile.Handle == DAIS_BAD_FILE) {
            printf("Failed to load default avatar.\n");
            exit(-1);
        } else {
            printf("Loaded default avatar, %u bytes at %p.\n", State->SkeletonFile.Size, State->SkeletonFile.Data);
            State->SkinnedMesh = LoadMeshData(&State->GameArena, State->SkeletonFile.Data);
            UploadMeshesToOGL(State->SkinnedMesh);
            State->Normals = GenerateNormalMeshes(&State->GameArena, &State->TempArena, State->SkinnedMesh);
        }

        State->LastCursorPos = vec2(
            (f32) Input->CursorX / Input->WindowWidth,
            (f32) Input->CursorY / Input->WindowHeight);
    }

    if (Platform->JustReloaded) {
        State->ShaderState = InitShaders(&State->GameArena);
    }


    // ---------- Input Handling ----------

    vec2 CursorPos = vec2((f32) Input->CursorX / Input->WindowWidth,
                          (f32) Input->CursorY / Input->WindowHeight);

    if (Input->LeftMouseButton.Pressed) {
        State->CamPos += (CursorPos - State->LastCursorPos);
        State->CamPos.x = glm::fract(State->CamPos.x);
        State->CamPos.y = glm::clamp(State->CamPos.y, -0.499f, 0.499f);
    }

    State->LastCursorPos = CursorPos;


    // -------- Prepare Matrices ---------

    float Aspect = (float) Input->WindowWidth / Input->WindowHeight;
    float HalfHeight = 100;
    float HalfDepth = 100 * HalfHeight;
    mat4 Projection = glm::ortho(
        -HalfHeight * Aspect, HalfHeight * Aspect, // left, right
        -HalfHeight, HalfHeight, // bottom, top
        0.0f, 2*HalfDepth // near, far
    );

    float Yaw = -State->CamPos.x * 360;
    float Pitch = State->CamPos.y * 180;
    vec3 LookDirection = vec3(0, 0, 1);
    LookDirection = glm::rotateX(LookDirection, Pitch);
    LookDirection = glm::rotateY(LookDirection, Yaw);

    vec3 LookCenter = vec3(0, HalfHeight, 0);
    vec3 LookSource = LookCenter - LookDirection * HalfDepth;

    mat4 View = glm::lookAt(
        LookSource,
        LookCenter,
        vec3(0.0f, 1.0f, 0.0f)
    );

    mat4 Combined = Projection * View;


    // -------- Rendering ---------

    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    RenderSkinnedMesh(State->ShaderState, State->SkinnedMesh, Combined);
    //RenderNormals(State->ShaderState, State->Normals, State->SkinnedMesh->MeshCount, Combined);
    glDisable(GL_DEPTH_TEST);
    RenderBones(State->ShaderState, &State->SkinnedMesh->BindPose, Combined);


    // ---------- Cleanup -----------

    if (State->TempArena.Pos > State->TempArenaMaxSize) {
        State->TempArenaMaxSize = State->TempArena.Pos;
        printf("New TempArena Max Size: %u\n", State->TempArena.Pos);
    }
    ArenaClear(&State->TempArena);
}

