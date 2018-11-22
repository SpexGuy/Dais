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
#include "imgui/imgui_demo.cpp"

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

// -------- Constants --------

ImGuiWindowFlags ImGuiStaticPane =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse;

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

    floor_grid Grid;
    skinned_mesh *SkinnedMesh;
    // use a pointer so we can hotswap a size change
    shader_state *ShaderState;

    animation *Anim;

    vec2 CamPos;

    float Angle;
    float AnimTime;

    float ClipStart;
    float ClipEnd;

    float AnimSpeed;

    u32 LastPressID;
    u32 CurrentAnimation;
    dais_listing AnimationsList;

    float ViewStart;
    float ViewEnd;

    bool TestLoop;
    bool RenderSkeleton;
    bool RenderGrid;
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
        State->ClipStart = 0;
        State->ClipEnd = State->Anim->Duration;
        State->ViewStart = 0;
        State->ViewEnd = State->Anim->Duration;
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

        InitFloorGrid(&State->Grid);

        State->AnimationsList = Platform->ListDirectory("../Avatar/Animations", PermArena);

        State->AnimSpeed = 1.0f;

        State->RenderGrid = true;

        LoadNextAnimation();
    }

    if (Platform->JustReloaded) {
        State->ShaderState = InitShaders(&State->GameArena);
    }


    // ---------- ImGUI ----------
    PERF_STAT(ImGUI);
    static bool showing = true;
    ImGui::ShowDemoWindow(&showing);

    // options
    float OptionsWidth = Input->WindowWidth * 0.2f;
    ImVec2 TopRight = ImVec2(Input->WindowWidth, 0);
    ImVec2 TRPivot = ImVec2(1.0f, 0.0f);
    ImVec2 WindowSize = ImVec2(OptionsWidth, Input->WindowHeight);
    ImGui::SetNextWindowPos(TopRight, ImGuiCond_Always, TRPivot);
    ImGui::SetNextWindowSize(WindowSize, ImGuiCond_Always);
    ImGui::Begin("Window", 0, ImGuiStaticPane);
    int AnimIndex = State->CurrentAnimation;
    ImGui::Combo("Current Animation", &AnimIndex, State->AnimationsList.Names, State->AnimationsList.Count);
    if (AnimIndex != State->CurrentAnimation) {
        State->CurrentAnimation = AnimIndex;
        LoadNextAnimation();
    }

    ImGui::SliderFloat("Speed", &State->AnimSpeed, 0, 3);
    if (ImGui::Button("Reset Speed")) {
        State->AnimSpeed = 1.0f;
    }

    if (ImGui::Button("Crop")) {
        State->ViewStart = State->ClipStart;
        State->ViewEnd = State->ClipEnd;
    }
    ImGui::SameLine();
    if (ImGui::Button("Zoom Out")) {
        float Scale = State->ViewEnd - State->ViewStart;
        State->ViewEnd += Scale * 0.1f;
        State->ViewStart -= Scale * 0.1f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Zoom In")) {
        float Scale = State->ViewEnd - State->ViewStart;
        State->ViewEnd -= Scale * 0.1f;
        State->ViewStart += Scale * 0.1f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        State->ViewStart = 0;
        State->ViewEnd = State->Anim->Duration;
    }

    ImGui::Checkbox("Test Loop", &State->TestLoop);
    ImGui::Checkbox("Render Grid", &State->RenderGrid);
    ImGui::Checkbox("Render Skeleton", &State->RenderSkeleton);

    if (State->ViewStart < 0) State->ViewStart = 0;
    if (State->ViewEnd > State->Anim->Duration) State->ViewEnd = State->Anim->Duration;
    if (State->ViewEnd <= State->ViewStart) State->ViewEnd = State->ViewStart + 0.01f;

    ImGui::End();

    // timeline
    float TimelineHeight = 82;
    ImVec2 TimelineSize = ImVec2(Input->WindowWidth - OptionsWidth, TimelineHeight);
    ImVec2 BottomLeft = ImVec2(0.0f, Input->WindowHeight);
    ImVec2 BLPivot = ImVec2(0.0f, 1.0f);
    ImGui::SetNextWindowPos(BottomLeft, ImGuiCond_Always, BLPivot);
    ImGui::SetNextWindowSize(TimelineSize);
    ImGui::Begin("Timeline", 0, ImGuiStaticPane);
    ImGui::SliderFloat("Start", &State->ClipStart, State->ViewStart, State->ViewEnd);
    ImGui::SliderFloat("Time", &State->AnimTime, State->ViewStart, State->ViewEnd);
    ImGui::SliderFloat("End", &State->ClipEnd, State->ViewStart, State->ViewEnd);
    ImGui::End();

    PERF_END(ImGUI);


    // ---------- Input Handling ----------

    PERF_STAT(Input);

    if (Input->LeftMouseButton.Pressed) {
        vec2 CursorDelta = vec2(
            (f32) Input->CursorDeltaX / Input->WindowWidth,
            (f32) Input->CursorDeltaY / Input->WindowHeight);
        State->CamPos -= CursorDelta;
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

    if (State->ClipEnd <= State->ClipStart) State->ClipEnd = State->ClipStart + 0.001f;
    if (State->AnimTime < State->ClipStart) State->AnimTime = State->ClipStart;


    float AnimDelta = Input->FrameDeltaSec * State->AnimSpeed;
    float ClipLength = State->ClipEnd - State->ClipStart;
    float RelativePos = State->AnimTime - State->ClipStart;
    RelativePos = fmod(RelativePos + AnimDelta, ClipLength);

    if (State->TestLoop) { 
        // when loop testing, calculate a half-second
        // zone on each end of the loop.  If the current
        // time is between these two zones, move the
        // animation time to the start of the later zone.

        // the time on each end of the loop
        float TestTimeSecHalf = 0.5f;
        float TestTimeAnimHalf = TestTimeSecHalf * State->AnimSpeed;
        if (RelativePos > TestTimeAnimHalf &&
                RelativePos < ClipLength - TestTimeAnimHalf) {
            RelativePos = ClipLength - TestTimeAnimHalf;
        }
    }

    State->AnimTime = State->ClipStart + RelativePos;

    f32 AnimPercent = State->AnimTime / State->Anim->Duration;
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
    float ClipSpaceLeft = OptionsWidth / Input->WindowWidth;
    float ClipSpaceUp = TimelineHeight / Input->WindowHeight;

    float Aspect = (float) Input->WindowWidth / Input->WindowHeight;
    float Distance = 400;
    mat4 Projection = glm::perspective(
        30.0f, Aspect, // fov, aspect
        0.1f, 2*Distance // near, far
    );

    Projection = glm::translate(vec3(-ClipSpaceLeft, ClipSpaceUp, 0.0f)) * Projection;

    float Yaw = -State->CamPos.x * 360;
    float Pitch = State->CamPos.y * 180;
    vec3 LookDirection = vec3(0, 0, 1);
    LookDirection = glm::rotateX(LookDirection, Pitch);
    LookDirection = glm::rotateY(LookDirection, Yaw);

    vec3 LookCenter = Skel.WorldMatrices[2] *
            vec4(Skel.WorldSetupMatrices[3][3], 1.0f);
    LookCenter.y -= 15;
    vec3 LookSource = LookCenter - LookDirection * Distance;

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

    if (State->RenderGrid) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        RenderGrid(State->ShaderState, &State->Grid, vec2(LookCenter.x, LookCenter.z), 5, Combined);
        glDisable(GL_BLEND);
    }

    RenderSkinnedMesh(State->ShaderState, State->SkinnedMesh, &Skel, Combined);

    if (State->RenderSkeleton) {
        glDisable(GL_DEPTH_TEST);
        RenderBones(State->ShaderState, &Skel, Combined);
    }

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

