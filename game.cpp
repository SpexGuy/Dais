// -------- Library Includes ---------

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb/stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/quaternion.hpp>

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::quat;
using glm::mat3;
using glm::mat4;
using glm::mat4x3;


// -------- Dais Includes --------

#include "dais.h"
#include "dais_render.h"
#include "arena.cpp"


// -------- Global Variables --------

memory_arena *TempArena;
memory_arena *PermArena;

// -------- Source Files --------

#include "strings.cpp"
#include "animation_types.h"
#include "animation.cpp"
#include "render.cpp"

struct state {
    u32 TempArenaMaxSize;
    memory_arena TempArena;
    memory_arena GameArena;
    dais_file SkeletonFile;
    dais_file AnimFile;

    skinned_mesh *SkinnedMesh;
    // use a pointer so we can hotswap a size change
    shader_state *ShaderState;

    animation *Anim;

    vec2 LastCursorPos;
    vec2 CamPos;

    float Angle;
    float AnimTime;
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
        }

        State->AnimFile = Platform->MapReadOnlyFile("../Avatar/Animations/Idle_JumpDownLow_BackFlip_Idle.ska");
        //State->AnimFile = Platform->MapReadOnlyFile("../Avatar/Animations/SmallStep.ska");
        //State->AnimFile = Platform->MapReadOnlyFile("../Avatar/Animations/Run_wallPush.ska");
        if (State->AnimFile.Handle == DAIS_BAD_FILE) {
            printf("Failed to load animation.\n");
            exit(-1);
        } else {
            printf("Loaded animation\n");
            State->Anim = LoadAnimation(&State->GameArena, State->AnimFile.Data);
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

    State->Angle += Input->FrameDeltaSec * 90;
    State->AnimTime += Input->FrameDeltaSec / 4;
    State->AnimTime = glm::fract(State->AnimTime);


    // --------- Animation ---------

    skeleton Skel;
    Skel.Pose = State->SkinnedMesh->BindPose;
    Skel.LocalSetupMatrices = ArenaAllocTN(TempArena, mat4x3, Skel.Pose.BoneCount);
    Skel.InverseLocalSetupMatrices = ArenaAllocTN(TempArena, mat4x3, Skel.Pose.BoneCount);
    Skel.WorldSetupMatrices = ArenaAllocTN(TempArena, mat4x3, Skel.Pose.BoneCount);
    Skel.InverseSetupMatrices = ArenaAllocTN(TempArena, mat4x3, Skel.Pose.BoneCount);
    UpdateSetupMatrices(&Skel);

    Skel.LocalTransforms = ArenaAllocTN(TempArena, transform, Skel.Pose.BoneCount);
    for (u32 Index = 0; Index < Skel.Pose.BoneCount; Index++) {
        Skel.LocalTransforms[Index] = Skel.Pose.SetupPose[Index];
    }

    SetAnimationToPercent(&Skel, State->Anim, State->AnimTime);

    // Skel.LocalTransforms[32].Rotation = Skel.LocalTransforms[32].Rotation *
    //     glm::angleAxis(
    //         State->Angle,
    //         glm::normalize(vec3(1,1,-1)));
    // Skel.LocalTransforms[33].Rotation = Skel.LocalTransforms[33].Rotation *
    //     glm::angleAxis(
    //         State->Angle,
    //         glm::normalize(vec3(1,1,-1)));
    // Skel.LocalTransforms[51].Rotation = Skel.LocalTransforms[51].Rotation *
    //     glm::angleAxis(
    //         State->Angle,
    //         glm::normalize(vec3(1,1,1)));
    // Skel.LocalTransforms[52].Rotation = Skel.LocalTransforms[52].Rotation *
    //     glm::angleAxis(
    //         State->Angle,
    //         glm::normalize(vec3(1,1,1)));
    Skel.LocalOffsets = ArenaAllocTN(TempArena, mat4x3, Skel.Pose.BoneCount);
    Skel.LocalMatrices = ArenaAllocTN(TempArena, mat4x3, Skel.Pose.BoneCount);
    Skel.CompositeMatrices = ArenaAllocTN(TempArena, mat4x3, Skel.Pose.BoneCount);
    Skel.WorldMatrices = ArenaAllocTN(TempArena, mat4x3, Skel.Pose.BoneCount);

    UpdateMatricesFromTransforms(&Skel);


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

    vec3 LookCenter = Skel.WorldMatrices[2] *
            vec4(Skel.WorldSetupMatrices[3][3], 1.0f);
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

    RenderSkinnedMesh(State->ShaderState, State->SkinnedMesh, &Skel, Combined);
    glDisable(GL_DEPTH_TEST);
    RenderBones(State->ShaderState, &Skel, Combined);


    // ---------- Cleanup -----------

    if (State->TempArena.Pos > State->TempArenaMaxSize) {
        u32 Size = State->TempArena.Pos;
        State->TempArenaMaxSize = Size;
        u32 Megabytes = (Size >> 20) & 0x3FF;
        u32 Kilobytes = (Size >> 10) & 0x3FF;
        u32 Bytes = Size & 0x3FF;
        if (Megabytes > 0) {
            printf("New TempArena Max Size: %d.%04d.%04d\n", Megabytes, Kilobytes, Bytes);
        } else if (Kilobytes > 0) {
            printf("New TempArena Max Size: %d.%04d\n", Kilobytes, Bytes);
        } else {
            printf("New TempArena Max Size: %d\n", Bytes);
        }
    }
    ArenaClear(&State->TempArena);
}

