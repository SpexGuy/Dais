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

#include "imgui/imgui.h"

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

struct state;

dais *PlatformRef;
state *State;
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

    u32 LastPressID;
    u32 CurrentAnimation;
    dais_listing AnimationsList;
};

#define TEMP_MEM_SIZE Megabytes(2)
#define GAME_OFFSET Kilobytes(4)

#define PERF_STAT(NAME) \
    dais_perf_stat NAME##Stat__ (PlatformRef, #NAME)
#define PERF_END(NAME) \
    NAME##Stat__.end()

static
void LoadNextAnimation() {
    printf("Loading next animation (id %d)\n", State->CurrentAnimation);
    char *Animation = State->AnimationsList.Names[State->CurrentAnimation];
    char *Filename = TCat("../Avatar/Animations/", Animation);
    if (State->AnimFile.Data) {
        printf("Unloading old animation\n");
        PlatformRef->UnmapReadOnlyFile(State->AnimFile.Handle);
    }

    printf("Loading %s\n", Animation);
    State->AnimFile = PlatformRef->MapReadOnlyFile(Filename);
    if (State->AnimFile.Handle == DAIS_BAD_FILE) {
        printf("Failed to load animation.\n");
        exit(-1);
    } else {
        State->Anim = LoadAnimation(PermArena, State->AnimFile.Data);
        State->AnimTime = 0;
    }
}

extern "C"
DAIS_UPDATE_AND_RENDER(GameUpdate) {
    Assert(GAME_OFFSET > sizeof(state));

    // ---------- Initialization ----------
    State = (state *) Platform->Memory;
    TempArena = &State->TempArena;
    PermArena = &State->GameArena;
    PlatformRef = Platform;

    PERF_STAT(Frame);

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

        State->LastCursorPos = vec2(
            (f32) Input->CursorX / Input->WindowWidth,
            (f32) Input->CursorY / Input->WindowHeight);

        State->AnimationsList = Platform->ListDirectory("../Avatar/Animations", PermArena);

        LoadNextAnimation();
    }

    if (Platform->JustReloaded) {
        State->ShaderState = InitShaders(&State->GameArena);
    }


    // ---------- ImGUI ----------
    PERF_STAT(ImGUI);
    ImGui::Begin("Window");
    int AnimIndex = State->CurrentAnimation;
    ImGui::Combo("Current Animation", &AnimIndex, State->AnimationsList.Names, State->AnimationsList.Count);
    if (AnimIndex != State->CurrentAnimation) {
        State->CurrentAnimation = AnimIndex;
        LoadNextAnimation();
    }
    ImGui::End();
    PERF_END(ImGUI);


    // ---------- Input Handling ----------

    PERF_STAT(Input);
    vec2 CursorPos = vec2((f32) Input->CursorX / Input->WindowWidth,
                          (f32) Input->CursorY / Input->WindowHeight);

    if (Input->LeftMouseButton.Pressed) {
        State->CamPos += (CursorPos - State->LastCursorPos);
        State->CamPos.x = glm::fract(State->CamPos.x);
        State->CamPos.y = glm::clamp(State->CamPos.y, -0.499f, 0.499f);
    }

    if (Input->UnassignedButton0.Pressed &&
        Input->UnassignedButton0.ModCount != State->LastPressID) {
        State->LastPressID = Input->UnassignedButton0.ModCount;
        State->CurrentAnimation++;
        if (State->CurrentAnimation >= State->AnimationsList.Count) {
            State->CurrentAnimation = 0;
        }
        LoadNextAnimation();
    }

    State->LastCursorPos = CursorPos;

    State->Angle += Input->FrameDeltaSec * 90;
    State->AnimTime += Input->FrameDeltaSec;
    f32 AnimPercent = glm::fract(State->AnimTime / State->Anim->Duration);
    PERF_END(Input);


    // --------- Animation ---------

    PERF_STAT(Animation);
    skeleton Skel;
    PERF_STAT(AnimationBake);
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
    PERF_END(AnimationBake);

    SetAnimationToPercent(&Skel, State->Anim, AnimPercent);

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
    PERF_END(Animation);


    // -------- Prepare Matrices ---------

    PERF_STAT(Matrices);
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
    PERF_END(Matrices);


    // -------- Rendering ---------

    PERF_STAT(Rendering);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    RenderSkinnedMesh(State->ShaderState, State->SkinnedMesh, &Skel, Combined);
    glDisable(GL_DEPTH_TEST);
    RenderBones(State->ShaderState, &Skel, Combined);
    PERF_END(Rendering);

    // ---------- Cleanup -----------

    PERF_STAT(Cleanup);
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
    PERF_END(Cleanup);
}

