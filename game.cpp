#include "dais.h"

#include <GL/glew.h>
#include <stdio.h>

#include "arena.cpp"

struct state {
    u64 LastPrintTime;
    f32 RedValue;
    memory_arena TempArena;
    memory_arena GameArena;
    u32 NumPoints;
    u32 Points[0];
};

state *GState;
dais *GPlatform;
dais_input *GInput;


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
    GState = State;
    GPlatform = Platform;
    GInput = Input;

    State->RedValue += Input->FrameDeltaSec * 0.25f;
    if (State->RedValue > 1.0f) {
        State->RedValue -= 1.0f;
    }

    f32 g = (f32) Input->CursorX / Input->WindowWidth;
    f32 b = (f32) Input->CursorY / Input->WindowHeight;
    glClearColor(0.0f, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    State->Points[(State->NumPoints << 1) + 0] = Input->CursorX * 2;
    State->Points[(State->NumPoints << 1) + 1] = (Input->WindowHeight - Input->CursorY) * 2;
    State->NumPoints++;

    glEnable(GL_SCISSOR_TEST);
    for (u32 c = 0; c < State->NumPoints; c++) {
        glScissor(State->Points[(c<<1)+0], State->Points[(c<<1)+1], 10, 10);
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);

    // u64 Now = Input->SystemTimeMS;
    // if (Now - State->LastPrintTime >= 500) {
    //     printf("Game Update at\n  system=%13lld\n  uptime=%13lld\n   delta=%13d\n  fdelta=%13f\n  basept=%p\n",
    //         Input->SystemTimeMS, Input->UpTimeMS, Input->FrameDeltaMS, Input->FrameDeltaSec, Platform->Memory);
    //     State->LastPrintTime += 500;
    // }
}

