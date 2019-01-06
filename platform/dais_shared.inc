

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

DAIS_SUBMIT_STAT(SubmitPerfStat) {
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
DAIS_LOAD_FILE_BUFFER(LoadFileBuffer) {
    dais_file File = {};

    File.Handle = DAIS_BAD_FILE;

    return File;
}

static
DAIS_FREE_FILE_BUFFER(FreeFileBuffer) {

}

static
DAIS_LOAD_FILE_BUFFER(MapReadOnlyFile) {
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

DAIS_FREE_FILE_BUFFER(UnmapReadOnlyFile) {
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

DAIS_LIST_DIRECTORY(ListDirectory) {
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


// ----------------- Input Recording ----------------

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
        exit(-1);
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
    fflush(stdout);

    glfwSwapInterval(1);

    PCALL(TimerInit)();

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
    Platform.Memory = (char *) PCALL(ReserveMemPages)((void *)DAIS_BASE_ADDRESS, Platform.MemorySize);

    if (!Platform.Memory) {
        int err = errno;
        printf("FATAL: Could not allocate 0x%llX bytes at address %p, error=%d (%s)\n", Platform.MemorySize, (void *)DAIS_BASE_ADDRESS, err, strerror(err));
        exit(-1);
    }

    Platform.LoadFileBuffer = LoadFileBuffer;
    Platform.FreeFileBuffer = FreeFileBuffer;
    Platform.MapReadOnlyFile = MapReadOnlyFile;
    Platform.UnmapReadOnlyFile = UnmapReadOnlyFile;
    Platform.ListDirectory = ListDirectory;
    Platform.ReadPerformanceCounter = PCALL(NanoTime);
    Platform.SubmitPerfStat = SubmitPerfStat;

    Platform.ContinueRunning = true;

    target_dylib Target = {};
    Target.LastModified = 1;

    u64 GameStartTime = PCALL(MilliTime)();
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
        u64 DylibLastModified = PCALL(GetLastModifiedTime)(DAIS_TARGET_STR);
        if (Target.LastModified == 1 || (DylibLastModified != 0 && DylibLastModified != Target.LastModified)) {
            PCALL(UpdateTarget)(&Target);
            Platform.JustReloaded = true;
            ClearPerfStats();
            PerfFrames = 0;
            LastPerfReport = PCALL(MilliTime)();
        } else {
            Platform.JustReloaded = false;
        }

        u64 FrameTime = PCALL(MilliTime)();

        FrameInput.SystemTimeMS = PCALL(SystemTime)();
        FrameInput.UpTimeMS = FrameTime - GameStartTime;
        FrameInput.FrameDeltaMS = (u32) (FrameTime - LastFrameTime);
        FrameInput.FrameDeltaSec = FrameInput.FrameDeltaMS * 0.001f;

        int CursorStartX = FrameInput.CursorX;
        int CursorStartY = FrameInput.CursorY;
        // call our input callbacks from GLFW
        // ideally we would do this after StartImguiFrame
        // so that we could update WantCapture*
        // but we need to set up inputs first
        // so this needs to come before.
        // This causes an extra frame of lag
        // which hopefully isn't so bad.
        glfwPollEvents();

        if (io.WantCaptureMouse) {
            FrameInput.CursorDeltaX = 0;
            FrameInput.CursorDeltaY = 0;
        } else {
            FrameInput.CursorDeltaX = CursorStartX - FrameInput.CursorX;
            FrameInput.CursorDeltaY = CursorStartY - FrameInput.CursorY;
        }

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

        fflush(stdout);

        LastFrameTime = FrameTime;
    }
}