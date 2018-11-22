#include "dais.h"

#include <stdio.h>
#include <string.h> // strerror

#include <mach/mach_init.h>
#include <mach/mach_time.h> // mach_absolute_time, for accurate monotonic time
#include <sys/dir.h> // opendir et al
#include <sys/mman.h> // mmap
#include <sys/time.h> // gettimeofday, for system time
#include <sys/types.h> // struct dirent
#include <sys/stat.h>
#include <errno.h>
#include <time.h> // clock, for cpu time
#include <copyfile.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

#include "arena.cpp"


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


// ----------------- Perf ----------------

struct perf_stat {
    u64 TotalTime;
    u32 TotalCount;
};

#define MAX_PERF_STATS 200
u32 UsedPerfStats = 0;
const char *StatNames[MAX_PERF_STATS];
u64 StatTimes[MAX_PERF_STATS];
u64 StatMax[MAX_PERF_STATS];
u32 StatCounts[MAX_PERF_STATS];

DAIS_SUBMIT_STAT(OSXPerfStat) {
    u32 Index;
    for (Index = 0; Index < UsedPerfStats; Index++) {
        // assume strings are reference equal, since they are the same point in code.
        if (StatNames[Index] == Name) {
            break;
        }
    }

    if (Index == UsedPerfStats) {
        if (UsedPerfStats < MAX_PERF_STATS) {
            UsedPerfStats++;
            StatNames[Index] = Name;
            StatTimes[Index] = Time;
            StatMax[Index] = Time;
            StatCounts[Index] = 1;
        }
    } else {
        StatTimes[Index] += Time;
        if (Time > StatMax[Index]) StatMax[Index] = Time;
        StatCounts[Index]++;
    }
}

static
void ClearPerfStats() {
    UsedPerfStats = 0;
}

static
void PrintPerfTime(u64 Nanos) {
    if (Nanos < 100000UL) {
        printf("%7llunS", Nanos);
    } else if (Nanos < 100000000UL) {
        printf("%7lluuS", Nanos / 1000);
    } else if (Nanos < 100000000000UL) {
        printf("%7llumS", Nanos / 1000000UL);
    } else {
        printf("%7llu S", Nanos / 1000000000UL);
    }
}

static
void PrintPerfReport(u32 NumFrames) {
    if (UsedPerfStats != 0) {
        printf("\n------- Perf Stats -------\n");
        printf(" PerFrame  Average      Max    Count  Name\n");
        for (u32 Index = 0; Index < UsedPerfStats; Index++) {
            const char *Name = StatNames[Index];
            u64 Total = StatTimes[Index];
            u64 Max = StatMax[Index];
            u32 Count = StatCounts[Index];
            u64 PerFrame = Total / NumFrames;
            u64 Average = Total / Count;
            PrintPerfTime(PerFrame);
            PrintPerfTime(Average);
            PrintPerfTime(Max);
            printf("  %7u  %s\n",
                Count, Name);
        }
        printf("\n");
        ClearPerfStats();
    }
}


// ----------------- Files ----------------

struct mapped_file {
    mapped_file *Next;
    s32 Handle;
    s32 NativeHandle;
    u32 MappedSize;
    void *MappedData;
};

struct file_state {
    u32 NextHandle;
    mapped_file *MappedFiles;
} FileState;

static inline
void PrintErrno() {
    int Err = errno;
    printf("  errno=%d (%s)\n", Err, strerror(Err));
}

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
    dais_file File = {};

    File.Handle = DAIS_BAD_FILE;

    return File;
}

static
DAIS_FREE_FILE_BUFFER(OSXFreeFileBuffer) {

}

static
DAIS_LOAD_FILE_BUFFER(OSXMapReadOnlyFile) {
    dais_file File = {};

    File.Handle = DAIS_BAD_FILE;

    int NativeHandle = open(Filename, O_RDONLY);
    if (NativeHandle == -1) {
        printf("Couldn't open %s\n", Filename);
        PrintErrno();
    } else {
        struct stat Stats = {};
        if (fstat(NativeHandle, &Stats) == -1) {
            printf("Couldn't stat %s\n", Filename);
            PrintErrno();
            close(NativeHandle);
        } else {
            File.Size = Stats.st_size;
            void* MappedData = mmap(
                0, // address
                File.Size, // length
                PROT_READ, // protection flags
                MAP_PRIVATE|MAP_FILE, // map flags
                NativeHandle, // file descriptor
                0); // offset

            if (MappedData == MAP_FAILED) {
                printf("Couldn't mmap %u bytes for %s\n", File.Size, Filename);
                PrintErrno();
                close(NativeHandle);
            } else {
                File.Data = MappedData;
                File.Handle = ++FileState.NextHandle;

                // record metadata so we can unmap the region later
                mapped_file *FileMeta = (mapped_file *) calloc(sizeof(mapped_file), 1);
                Assert(FileMeta);
                FileMeta->Handle = File.Handle;
                FileMeta->NativeHandle = NativeHandle;
                FileMeta->MappedSize = File.Size;
                FileMeta->MappedData = MappedData;
                FileMeta->Next = FileState.MappedFiles;
                FileState.MappedFiles = FileMeta;
            }
        }
    }

    return File;
}

DAIS_FREE_FILE_BUFFER(OSXUnmapReadOnlyFile) {
    // find the file metadata
    mapped_file **Curr = &FileState.MappedFiles;
    while (*Curr) {
        if ((*Curr)->Handle == Handle) break;
        Curr = &((*Curr)->Next);
    }
    
    if (*Curr) {
        // unlink the metadata
        mapped_file *FileMeta = *Curr;
        *Curr = FileMeta->Next;

        // unmap the memory
        munmap(FileMeta->MappedData, FileMeta->MappedSize);
        close(FileMeta->NativeHandle);

        // free the metadata
        free(FileMeta);
    }
}

static inline
bool KeepListEntry(char *Filename) {
    return strcmp(Filename, ".") != 0 &&
            strcmp(Filename, "..") != 0;
}

DAIS_LIST_DIRECTORY(OSXListDirectory) {
    dais_listing Result = {};
    Result.Count = -1;

    // open the directory
    DIR *Dirp = opendir(Dir);
    if (Dirp) {
        struct dirent *DirEnt;

        // count the number of items
        s32 Count = 0;
        DirEnt = readdir(Dirp);
        while (DirEnt) {
            if (KeepListEntry(DirEnt->d_name)) {
                Count++;
            }
            DirEnt = readdir(Dirp);
        }

        // copy the data from the items
        Result.Count = Count;
        if (Count > 0) {
            // make space for the names
            Result.Names = ArenaAllocTN(Arena, char *, Count);

            // restart the directory iterator
            rewinddir(Dirp);

            // copy the name of each item
            // in the event that the directory contents changed
            // (this may be impossible, but I can't find documentation
            //  clearly stating that it can't happen), make sure we
            // don't overrun the buffer we allocated.  When finished,
            // set the count to the actual number of items allocated.
            s32 Index = 0;
            DirEnt = readdir(Dirp);
            while (DirEnt && Index < Count) {
                if (KeepListEntry(DirEnt->d_name)) {
                    Result.Names[Index] = ArenaStrcpy(Arena, DirEnt->d_name);
                    Index++;
                }
                DirEnt = readdir(Dirp);
            }
            Result.Count = Index;
        }
        closedir(Dirp);
    }

    return Result;
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
dais_input PlaybackInput;

static
void GlfwWindowSizeCallback(GLFWwindow *window, int width, int height) {
    FrameInput.WindowWidth = width;
    FrameInput.WindowHeight = height;
}

static
void GlfwFramebufferSizeCallback(GLFWwindow *window, int width, int height) {
    glViewport(0, 0, width, height);
}

static
void GlfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (action == GLFW_PRESS)
        io.KeysDown[key] = true;
    if (action == GLFW_RELEASE)
        io.KeysDown[key] = false;

    (void)mods; // Modifiers are not reliable across systems
    io.KeyCtrl = io.KeysDown[GLFW_KEY_LEFT_CONTROL] || io.KeysDown[GLFW_KEY_RIGHT_CONTROL];
    io.KeyShift = io.KeysDown[GLFW_KEY_LEFT_SHIFT] || io.KeysDown[GLFW_KEY_RIGHT_SHIFT];
    io.KeyAlt = io.KeysDown[GLFW_KEY_LEFT_ALT] || io.KeysDown[GLFW_KEY_RIGHT_ALT];
    io.KeySuper = io.KeysDown[GLFW_KEY_LEFT_SUPER] || io.KeysDown[GLFW_KEY_RIGHT_SUPER];

    if (io.WantCaptureKeyboard) return;

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

    if (action != GLFW_REPEAT) {
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
}

static
void GlfwCursorPosCallback(GLFWwindow *window, double xPos, double yPos) {
    FrameInput.CursorX = (s32) xPos;
    FrameInput.CursorY = (s32) yPos;
}

static
void GlfwClickCallback(GLFWwindow *window, int button, int action, int mods) {
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    if (button < ElementCount(FrameInput.MouseButtons)) {
        FrameInput.MouseButtons[button].ModCount++;
        FrameInput.MouseButtons[button].Pressed = (action == GLFW_PRESS);
    }
}

static
void GlfwScrollCallback(GLFWwindow *window, double xOffset, double yOffset) {
    // scrolling is kind of fast, slow it down with this.
    // TODO: this value is probably related to the
    // display density. Do some digging and see
    // what feels good
    float ScrollScalar = 0.5f;
    ImGuiIO& io = ImGui::GetIO();
    io.MouseWheelH += (float)xOffset * ScrollScalar;
    io.MouseWheel += (float)yOffset * ScrollScalar * 0.2f;
    // The *0.2 above counteracts a *5 inside of ImGui.
    // I'm not sure why it's there, but it causes vertical
    // scrolling to be 5x faster than horizontal, which is
    // not fun to use.
}

static
void GlfwCharCallback(GLFWwindow*, unsigned int c) {
    ImGuiIO& io = ImGui::GetIO();
    if (c > 0 && c < 0x10000)
        io.AddInputCharacter((unsigned short)c);
}


static
void GlfwErrorCallback(int error, const char* description) {
    printf("GLFW Error: %s (error %d)\n", description, error);
}



// ----------------- Main ----------------

typedef int glad_loader_init(GLADloadproc LoadProc);

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
        glad_loader_init *GladInit = (glad_loader_init *)
                dlsym(Target->Handle, "gladLoadGLLoader");
        if (GladInit) {
            if (!GladInit((GLADloadproc) glfwGetProcAddress)) {
                printf("WARNING: Couldn't init GLAD loader in new dll\n");
            }
        } else {
            printf("WARNING: GLAD loader not found in new dll\n");
        }
    }

    if (!Target->UpdateAndRender) {
        Target->UpdateAndRender = StubUpdateAndRender;
        printf("WARNING: Couldn't load target %s\n", DAIS_TARGET_STR);
    } else {
        printf("INFO: Updating Game Code\n");
    }
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
dais_input *PreProcessInput() {
    dais_input *InputToUse = &FrameInput;

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
            ssize_t Res = read(RecordingState.File, &PlaybackInput, sizeof(PlaybackInput));
            if (Res == 0) {
                RestartPlayback();
                read(RecordingState.File, &PlaybackInput, sizeof(PlaybackInput));
            }
            InputToUse = &PlaybackInput;
        } break;
    }

    return InputToUse;
}

void StartImguiFrame() {
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

int main(int argc, char **argv) {
    if (!glfwInit()) {
        printf("FATAL: Could not initialize GLFW\n");
        return -1;
    }

    glfwSetErrorCallback(GlfwErrorCallback);

#if __APPLE__
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    // glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    // glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    Window = glfwCreateWindow(640, 480, "Dais Window", NULL, NULL);
    if (!Window) {
        printf("FATAL: Could not create window.\n");
        return -1;
    }

    glfwSetWindowSizeCallback(Window, GlfwWindowSizeCallback);
    glfwSetFramebufferSizeCallback(Window, GlfwFramebufferSizeCallback);
    glfwSetKeyCallback(Window, GlfwKeyCallback);
    glfwSetCharCallback(Window, GlfwCharCallback);
    glfwSetCursorPosCallback(Window, GlfwCursorPosCallback);
    glfwSetMouseButtonCallback(Window, GlfwClickCallback);
    glfwSetScrollCallback(Window, GlfwScrollCallback);

    glfwMakeContextCurrent(Window);

    if(!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
        printf("Couldn't initialize GLAD!\n");
        exit(-1);
    }
    printf("OpenGL %d.%d\n", GLVersion.major, GLVersion.minor);
    printf("OpenGL version recieved: %s\n", glGetString(GL_VERSION));

    glfwSwapInterval(1);

    mach_timebase_info(&Timebase);

    // Setup Dear ImGui binding
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

    ImGui_ImplGlfw_InitForOpenGL(Window, false);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Setup style
    ImGui::StyleColorsDark();

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
    Platform.MapReadOnlyFile = OSXMapReadOnlyFile;
    Platform.UnmapReadOnlyFile = OSXUnmapReadOnlyFile;
    Platform.ListDirectory = OSXListDirectory;
    Platform.ReadPerformanceCounter = OSXNanoTime;
    Platform.SubmitPerfStat = OSXPerfStat;

    Platform.ContinueRunning = true;

    target_dylib Target = {};
    Target.LastModified = 1;

    u64 GameStartTime = OSXMilliTime();
    u64 LastFrameTime = GameStartTime;
    u64 LastPerfReport = GameStartTime;
    u32 PerfFrames = 0;

    {
        // seed resize callbacks with inital size
        int Width, Height;
        glfwGetWindowSize(Window, &Width, &Height);
        GlfwWindowSizeCallback(Window, Width, Height);
        glfwGetFramebufferSize(Window, &Width, &Height);
        GlfwFramebufferSizeCallback(Window, Width, Height);
    }

    while (Platform.ContinueRunning && !glfwWindowShouldClose(Window)) {
        u64 DylibLastModified = GetLastModifiedTime(DAIS_TARGET_STR);
        if (DylibLastModified != 0 && DylibLastModified != Target.LastModified) {
            UpdateTarget(&Target);
            Platform.JustReloaded = true;
            ClearPerfStats();
            PerfFrames = 0;
            LastPerfReport = OSXMilliTime();
        } else {
            Platform.JustReloaded = false;
        }

        u64 FrameTime = OSXMilliTime();

        FrameInput.SystemTimeMS = OSXSystemTime();
        FrameInput.UpTimeMS = FrameTime - GameStartTime;
        FrameInput.FrameDeltaMS = (u32) (FrameTime - LastFrameTime);
        FrameInput.FrameDeltaSec = FrameInput.FrameDeltaMS * 0.001f;

        // call our input callbacks from GLFW
        // ideally we would do this after StartImguiFrame
        // so that we could update WantCapture*
        // but we need to set up inputs first
        // so this needs to come before.
        // This causes an extra frame of lag
        // which hopefully isn't so bad.
        glfwPollEvents();

        StartImguiFrame();

        dais_input *InputToUse = PreProcessInput();

        Target.UpdateAndRender(&Platform, InputToUse);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(Window);

        PerfFrames++;
        if (Platform.PrintPerformanceCounters &&
                FrameTime - LastPerfReport > 10000) {
            LastPerfReport = FrameTime;
            PrintPerfReport(PerfFrames);
            PerfFrames = 0;
        }

        LastFrameTime = FrameTime;
    }
}