#ifndef DAIS_H_
#define DAIS_H_

#include <stdint.h>
#include <stdlib.h> // exit, for Assert
#include <stdio.h> // printf, for Assert

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

typedef uintptr_t uptr;
typedef intptr_t sptr;

typedef float f32;
typedef double f64;

typedef int32_t b32;

#define Terabytes(x) ((u64)(x) * 1024 * 1024 * 1024 * 1024)
#define Gigabytes(x) ((u64)(x) * 1024 * 1024 * 1024)
#define Megabytes(x) ((u64)(x) * 1024 * 1024)
#define Kilobytes(x) ((u64)(x) * 1024)

#define Assert(x) do { if (!(x)) { printf("Assert failed! on line %d of %s, '%s'\n", __LINE__, __FILE__, #x); exit(42); } } while(0)

#define AlignRoundUp(x, sz) (((x) + (sz) - 1) & ~((sz) - 1))
#define IsPowerOfTwo(x) (((x) & ((x)-1)) == 0)

#define ElementCount(x) (sizeof(x) / sizeof(x[0]))

#define DAIS_BAD_FILE -1L

struct dais_file {
    s32 Handle;
    u32 Size;
    void *Data;
};

// Dais API typedefs
#define DAIS_LOAD_FILE_BUFFER(name) dais_file name(const char *Filename)
typedef DAIS_LOAD_FILE_BUFFER(dais_load_file_buffer);

#define DAIS_FREE_FILE_BUFFER(name) void name(s32 Handle)
typedef DAIS_FREE_FILE_BUFFER(dais_free_file_buffer);

#define DAIS_VOID_FN(name) void name(void)
typedef DAIS_VOID_FN(dais_void_fn);

struct dais {
    b32 Initialized;

    /** When this is set to false, dais will terminate. */
    b32 ContinueRunning;

    u64 MemorySize;
    char *Memory;

    /** Copies a file's contents into memory.
     *  If the file could not be loaded,
     *  its size will be a negative error code. */
    dais_load_file_buffer *LoadFileBuffer;

    /** Maps file's contents into memory.
     *  If the file could not be loaded,
     *  its handle will be a negative error code. */
    dais_load_file_buffer *MapReadOnlyFile;

    /** Frees a file buffer allocated in LoadFileBuffer.
     *  Only needs to be called if LoadFileBuffer succeeded. */
    dais_free_file_buffer *FreeFileBuffer;

    /** Frees a file buffer allocated in MapReadOnlyFile.
     *  Only needs to be called if MapReadOnlyFile succeeded. */
    dais_free_file_buffer *UnmapReadOnlyFile;
};

struct dais_button {
    u32 ModCount;
    b32 Pressed;
};

struct dais_input {
    u64 SystemTimeMS;
    u64 UpTimeMS;
    u32 FrameDeltaMS;
    f32 FrameDeltaSec;

    s32 WindowWidth;
    s32 WindowHeight;

    s32 CursorX;
    s32 CursorY;

    union {
        dais_button MouseButtons[3];
        struct {
            dais_button LeftMouseButton;
            dais_button RightMouseButton;
            dais_button MiddleMouseButton;
        };
    };

    union {
        dais_button Buttons[9];
        struct {
            dais_button UnassignedButton0; // mapped to E
            dais_button UnassignedButton1; // mapped to R
            dais_button UnassignedButton2; // mapped to T
            dais_button UnassignedButton3; // mapped to D
            dais_button UnassignedButton4; // mapped to F
            dais_button UnassignedButton5; // mapped to G
            dais_button UnassignedButton6; // mapped to C
            dais_button UnassignedButton7; // mapped to V
            dais_button UnassignedButton8; // mapped to B
        };
    };
    // TODO: keyboard and controller state
};


#define DAIS_UPDATE_AND_RENDER(name) void name(dais *Platform, dais_input *Input)
typedef DAIS_UPDATE_AND_RENDER(dais_update_and_render);

#endif
