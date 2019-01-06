#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_glfw.h"
#include "../imgui/imgui_impl_opengl3.h"

#include "../shared/arena.cpp"


// ---------------- Compile-time Config ----------------

#ifndef DAIS_BASE_ADDRESS
#define DAIS_BASE_ADDRESS 0x400000000UL
#endif

#ifndef DAIS_MEM_SIZE
#define DAIS_MEM_SIZE Gigabytes(1)
#endif

#ifndef DAIS_TARGET
#define DAIS_TARGET libgame.so
#endif

#ifndef DAIS_UPDATE_FUNC
#define DAIS_UPDATE_FUNC GameUpdate
#endif

#ifndef DAIS_INUSE
#define DAIS_INUSE dais_inuse.so
#endif

// These colors taken from https://sashat.me/2017/01/11/list-of-20-simple-distinct-colors/
const u32 DebugColors[] = {
    // in RGBA form
    0xe6194bff, // reddish
    0x3cb44bff, // green
    0xffe119ff, // yellow
    0x4363d8ff, // blue
    0xf58231ff, // orange
    0x911eb4ff, // purple
    0x42d4f4ff, // cyan
    0xf032e6ff, // magenta
    0xbfef45ff, // lime
    0xfabebeff, // pink
    0x469990ff, // lavender
    0x9a6324ff, // brown
    0xfffac8ff, // beige
    0x800000ff, // maroon
    0xaaffc3ff, // mint
    0x808000ff, // olive
    0xffd8b1ff, // apricot
    0x000075ff, // navy
    0xa9a9a9ff, // gray
    0xffffffff, // white
    0x000000ff  // black
};


// ---------------- Derivative Macros ------------------

#define STRINGIZE(x) #x
#define STRINGIZE_VALUE(x) STRINGIZE(x)

#define DAIS_TARGET_STR STRINGIZE_VALUE(DAIS_TARGET)
#define DAIS_INUSE_STR  STRINGIZE_VALUE(DAIS_INUSE)
#define DAIS_UPDATE_FUNC_STR STRINGIZE_VALUE(DAIS_UPDATE_FUNC)


// ----------------- Forward Declarations ------------------

u64 GetLastModifiedTime(const char *Filename);
