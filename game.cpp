#include "dais.h"

#include <GL/glew.h>
#include <stdio.h>

#include "arena.cpp"

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
    u32 NumPoints;
    point Points[0];
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
        Platform->Initialized = true;
    }

    State->RedValue += Input->FrameDeltaSec * 0.25f;
    if (State->RedValue > 1.0f) {
        State->RedValue -= 1.0f;
    }

    f32 g = (f32) Input->CursorX / Input->WindowWidth;
    f32 b = (f32) Input->CursorY / Input->WindowHeight;
    glClearColor(0.0f, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    u32 NumPoints = State->NumPoints++;
    u32 WriteIndex = NumPoints & 511;
    if (NumPoints & ~511) NumPoints = 512;

    point *Point = State->Points + WriteIndex;
    Point->X = Input->CursorX * 2;
    Point->Y = (Input->WindowHeight - Input->CursorY) * 2;
    Point->R = 1.0f;
    Point->G = g;
    Point->B = b;

    glEnable(GL_SCISSOR_TEST);
    for (u32 c = 0; c < NumPoints; c++) {
        point *P = State->Points + c;
        glScissor(P->X, P->Y, 10, 10);
        glClearColor(P->R, P->G, P->B, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);

    if (State->TempArena.Pos > State->TempArenaMaxSize) {
        State->TempArenaMaxSize = State->TempArena.Pos;
        printf("New TempArena Max Size: %u\n", State->TempArena.Pos);
    }
    ArenaClear(&State->TempArena);
}

