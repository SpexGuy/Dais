#include "dais.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <stdio.h>
#include <string.h>
#include <io.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

#include "win32_mman.h"

#define PCALL(X) MGW##X

#include "dais_shared.h"



// ----------------- Timing ----------------

static HANDLE CurrentProcess;
static LARGE_INTEGER QPCFrequency;


static inline
void MGWTimerInit() {
	CurrentProcess = GetCurrentProcess();
	QueryPerformanceFrequency(&QPCFrequency);
}

static
u64 MGWSystemTime() {
    SYSTEMTIME time;
	GetSystemTime(&time);
	u64 NowMS = (time.wSecond * 1000) + time.wMilliseconds;
    return NowMS;
}

static
u64 MGWNanoTime() {
	LARGE_INTEGER Time;
	QueryPerformanceCounter(&Time);
	return (u64) Time.QuadPart * 1000000000UL / QPCFrequency.QuadPart;
}

static
u64 MGWMilliTime() {
    u64 Now = MGWNanoTime() / 1000000UL;
    return Now;
}

static
u64 MGWNanoClock() {
	// cpu time can only be read at microsecond resolution
	// defer to QPC for profiling.
	return MGWNanoTime();
}

// ----------------- Files ----------------

static
u64 MGWGetLastModifiedTime(const char *Filename) {
    u64 LastWrite = 0;
    struct stat Stats;
    if (stat(Filename, &Stats) == 0) {
        LastWrite = Stats.st_mtime;
    }
    return LastWrite;
}


// ----------------- Memory ----------------

static
void *MGWReserveMemPages(void *RequestedAddress, u64 SizeBytes) {
    // ignore the requested address
    // 32-bit windows is pretty cramped.  Let the OS place the memory.
    return VirtualAlloc(0,
         SizeBytes,
         MEM_COMMIT,
         PAGE_READWRITE);
}




// ----------------- Hotswap -----------------

typedef int glad_loader_init(GLADloadproc LoadProc);

static
DAIS_UPDATE_AND_RENDER(StubUpdateAndRender) {
}

struct target_dylib {
    u64 LastModified;
    void *Handle;
    dais_update_and_render *UpdateAndRender;
};


static inline
void MGWUpdateTarget(target_dylib *Target) {
    if (Target->Handle) {
        dlclose(Target->Handle);
    }

    CopyFile(DAIS_TARGET_STR, DAIS_INUSE_STR, false);

    Target->LastModified = MGWGetLastModifiedTime(DAIS_INUSE_STR);
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


#include "dais_shared.inc"
