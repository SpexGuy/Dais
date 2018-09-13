#include "dais.h"

#include <stdio.h>
#include <string.h> // strerror

#include <mach/mach_init.h>
#include <mach/mach_time.h> // mach_absolute_time, for accurate monotonic time
#include <sys/mman.h> // mmap
#include <sys/time.h> // gettimeofday, for system time
#include <sys/stat.h>
#include <errno.h>
#include <time.h> // clock, for cpu time
#include <copyfile.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

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


// ---------------- Derivative Macros ------------------

#define STRINGIZE(x) #x
#define STRINGIZE_VALUE(x) STRINGIZE(x)

#define DAIS_TARGET_STR STRINGIZE_VALUE(DAIS_TARGET)
#define DAIS_INUSE_STR  STRINGIZE_VALUE(DAIS_INUSE)
#define DAIS_UPDATE_FUNC_STR STRINGIZE_VALUE(DAIS_UPDATE_FUNC)


#define RECORD_NONE 0
#define RECORD_WRITE 1
#define RECORD_READ 2
struct recording_state {
    int State;
    int File;
    int Advance;
};

dais Platform;

recording_state RecordingState;

GLFWwindow *Window;

// ----------------- Files ----------------

static
u64 GetLastModifiedTime(const char *Filename) {
    u64 LastWrite = 0;
    struct stat Stats;
    if (stat(Filename, &Stats) == 0) {
        LastWrite = Stats.st_mtimespec.tv_sec;
    }
    return LastWrite;
}

static
DAIS_LOAD_FILE_BUFFER(OSXLoadFileBuffer) {
    dais_file file = {};

    file.Size = DAIS_BAD_FILE;

    return file;
}

static
DAIS_FREE_FILE_BUFFER(OSXFreeFileBuffer) {

}


// ----------------- Timing ----------------

static mach_timebase_info_data_t Timebase = {};

static
u64 OSXSystemTime() {
    u64 Now = 0;
    struct timeval Tv = {};
    gettimeofday(&Tv, 0);
    Now = Tv.tv_sec * 1000UL + Tv.tv_usec / 1000UL;
    return Now;
}

static
u64 OSXNanoTime() {
    u64 Raw = mach_absolute_time();
    u64 Now = Raw * (u64) Timebase.numer / (u64) Timebase.denom;
    return Now;
}

static
u64 OSXMilliTime() {
    u64 Now = OSXNanoTime() / 1000000UL;
    return Now;
}

static
u64 OSXNanoClock() {
    u64 Now = clock() * (1000000000UL / CLOCKS_PER_SEC);
    return Now;
}


// ----------------- Input ----------------

dais_input FrameInput;

static
void GlfwResizeCallback(GLFWwindow *window, int width, int height) {
    glViewport(0, 0, width, height);
    FrameInput.WindowWidth = width;
    FrameInput.WindowHeight = height;
}

static
void GlfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, true);
        } else if (key == GLFW_KEY_W) {
            static bool Wireframe = false;
            Wireframe = !Wireframe;
            glPolygonMode(GL_FRONT_AND_BACK, Wireframe ? GL_LINE : GL_FILL);
        } else if (key == GLFW_KEY_P) {
            RecordingState.Advance = 1;
        }
    }

    int Keys[] = {
        GLFW_KEY_E,
        GLFW_KEY_R,
        GLFW_KEY_T,
        GLFW_KEY_D,
        GLFW_KEY_F,
        GLFW_KEY_G,
        GLFW_KEY_C,
        GLFW_KEY_V,
        GLFW_KEY_B
    };

    for (u32 c = 0; c < ElementCount(Keys); c++) {
        if (Keys[c] == key) {
            FrameInput.Buttons[c].ModCount++;
            FrameInput.Buttons[c].Pressed = (action != GLFW_RELEASE);
            break;
        }
    }
}

static
void GlfwCursorPosCallback(GLFWwindow *window, double xPos, double yPos) {
    FrameInput.CursorX = (s32) xPos;
    FrameInput.CursorY = (s32) yPos;
}

static
void GlfwClickCallback(GLFWwindow *window, int button, int action, int mods) {
    // TODO
    // mouse clicks
}

static
void GlfwErrorCallback(int error, const char* description) {
    printf("GLFW Error: %s (error %d)\n", description, error);
}



// ----------------- Main ----------------

struct target_dylib {
    u64 LastModified;
    void *Handle;
    dais_update_and_render *UpdateAndRender;
};

static
DAIS_UPDATE_AND_RENDER(StubUpdateAndRender) {
}

static inline
void UpdateTarget(target_dylib *Target) {
    if (Target->Handle) {
        dlclose(Target->Handle);
    }

    copyfile(DAIS_TARGET_STR, DAIS_INUSE_STR, 0, COPYFILE_ALL);

    Target->LastModified = GetLastModifiedTime(DAIS_INUSE_STR);
    Target->Handle = dlopen(DAIS_INUSE_STR, RTLD_LOCAL|RTLD_LAZY);
    Target->UpdateAndRender = 0;
    if (Target->Handle) {
        Target->UpdateAndRender = (dais_update_and_render *)
                dlsym(Target->Handle, DAIS_UPDATE_FUNC_STR);
    }

    if (!Target->UpdateAndRender) {
        Target->UpdateAndRender = StubUpdateAndRender;
        printf("WARNING: Couldn't load target %s\n", DAIS_TARGET_STR);
    } else {
        printf("INFO: Updating Game Code\n");
    }
}

static inline
void PrintErrno() {
    int Err = errno;
    printf("  errno=%d (%s)\n", Err, strerror(Err));
}

static inline
void StartRecording() {
    RecordingState.State = RECORD_NONE;
    RecordingState.File = open("record_log.bin",
        O_RDWR | O_TRUNC | O_CREAT,
        0666);
    if (RecordingState.File >= 0) {
        ssize_t Written = write(RecordingState.File, Platform.Memory, Platform.MemorySize);
        if (Written == Platform.MemorySize) {
            RecordingState.State = RECORD_WRITE;
        } else {
            printf("Couldn't start recording: full memory not written. Wrote %ld not %lld\n", Written, Platform.MemorySize);
            PrintErrno();
            int Res = close(RecordingState.File);
            if (Res < 0) {
                printf("Couldn't close recording file.\n");
                PrintErrno();
            }
            RecordingState.File = -1;
        }
    } else {
        printf("Couldn't start recording: failed to open file.\n");
        PrintErrno();
    }
}

static inline
void RestartPlayback() {
    int Res = lseek(RecordingState.File, 0, SEEK_SET);
    if (Res < 0) {
        printf("Couldn't seek in recording.\n");
        PrintErrno();
    }
    RecordingState.State = RECORD_READ;
    Res = read(RecordingState.File, Platform.Memory, Platform.MemorySize);
    if (Res < 0) {
        printf("Couldn't read back state.\n");
        PrintErrno();
    }
}

static inline
void StopPlayback() {
    int Res = close(RecordingState.File);
    if (Res < 0) {
        printf("Couldn't close recording file.\n");
        PrintErrno();
    }
    RecordingState.File = -1;
    RecordingState.State = RECORD_NONE;
}

static inline
void PreProcessInput() {
    if (RecordingState.Advance) {
        switch (RecordingState.State) {
        case RECORD_NONE:
            StartRecording();
            break;
        case RECORD_WRITE:
            RestartPlayback();
            break;
        case RECORD_READ:
            StopPlayback();
            break;
        }
        RecordingState.Advance = 0;
    }

    switch (RecordingState.State) {
        case RECORD_WRITE:
            write(RecordingState.File, &FrameInput, sizeof(FrameInput));
            break;
        case RECORD_READ: {
            ssize_t Res = read(RecordingState.File, &FrameInput, sizeof(FrameInput));
            if (Res == 0) {
                RestartPlayback();
                read(RecordingState.File, &FrameInput, sizeof(FrameInput));
            }
        } break;
    }
}

int main(int argc, char **argv) {
    if (!glfwInit()) {
        printf("FATAL: Could not initialize GLFW\n");
        return -1;
    }

    glfwSetErrorCallback(GlfwErrorCallback);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    Window = glfwCreateWindow(640, 480, "Dais Window", NULL, NULL);
    if (!Window) {
        printf("FATAL: Could not create window.\n");
        return -1;
    }

    glfwSetFramebufferSizeCallback(Window, GlfwResizeCallback);
    glfwSetKeyCallback(Window, GlfwKeyCallback);
    glfwSetCursorPosCallback(Window, GlfwCursorPosCallback);
    glfwSetMouseButtonCallback(Window, GlfwClickCallback);

    glfwMakeContextCurrent(Window);

    // If the program is crashing at glGenVertexArrays, try uncommenting this line.
    //glewExperimental = GL_TRUE;
    glewInit();

    printf("OpenGL version recieved: %s\n", glGetString(GL_VERSION));

    glfwSwapInterval(1);

    mach_timebase_info(&Timebase);

    Platform.Initialized = false;
    Platform.MemorySize = DAIS_MEM_SIZE;
    Platform.Memory = (char *) mmap((void *)DAIS_BASE_ADDRESS,
                        Platform.MemorySize,
                        PROT_READ | PROT_WRITE,
                        MAP_ANON | MAP_SHARED,
                        -1,
                        0);

    if (!Platform.Memory) {
        int err = errno;
        printf("FATAL: Could not allocate 0x%llX bytes at address %p, error=%d (%s)\n", Platform.MemorySize, Platform.Memory, err, strerror(err));
        return -1;
    }

    Platform.LoadFileBuffer = OSXLoadFileBuffer;
    Platform.FreeFileBuffer = OSXFreeFileBuffer;

    Platform.ContinueRunning = true;

    target_dylib Target = {};
    UpdateTarget(&Target);

    u64 GameStartTime = OSXMilliTime();
    u64 LastFrameTime = GameStartTime;

    glfwGetWindowSize(Window, &FrameInput.WindowWidth, &FrameInput.WindowHeight);

    while (Platform.ContinueRunning && !glfwWindowShouldClose(Window)) {
        u64 DylibLastModified = GetLastModifiedTime(DAIS_TARGET_STR);
        if (DylibLastModified != 0 && DylibLastModified != Target.LastModified) {
            UpdateTarget(&Target);
        }

        u64 FrameTime = OSXMilliTime();

        FrameInput.SystemTimeMS = OSXSystemTime();
        FrameInput.UpTimeMS = FrameTime - GameStartTime;
        FrameInput.FrameDeltaMS = (u32) (FrameTime - LastFrameTime);
        FrameInput.FrameDeltaSec = FrameInput.FrameDeltaMS * 0.001f;

        glfwPollEvents(); // this will call our callbacks and fill in FrameInput.

        PreProcessInput();

        Target.UpdateAndRender(&Platform, &FrameInput);

        glfwSwapBuffers(Window);

        LastFrameTime = FrameTime;
    }
}