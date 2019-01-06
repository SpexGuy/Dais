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

#define PCALL(X) OSX##X

#include "dais_shared.h"

// ----------------- Timing ----------------

static mach_timebase_info_data_t Timebase = {};

static inline
void OSXTimerInit() {
    mach_timebase_info(&Timebase);
}

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

// ----------------- Files ----------------

static
u64 OSXGetLastModifiedTime(const char *Filename) {
    u64 LastWrite = 0;
    struct stat Stats;
    if (stat(Filename, &Stats) == 0) {
        LastWrite = Stats.st_mtimespec.tv_sec;
    }
    return LastWrite;
}


// ----------------- Memory -----------------

static
void *OSXReserveMemPages(void *RequestedAddress, u64 SizeBytes) {
    mmap(RequestedAddress,
         SizeBytes,
         PROT_READ | PROT_WRITE,
         MAP_ANON | MAP_SHARED,
         -1,
         0);
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
void OSXUpdateTarget(target_dylib *Target) {
    if (Target->Handle) {
        dlclose(Target->Handle);
    }

    copyfile(DAIS_TARGET_STR, DAIS_INUSE_STR, 0, COPYFILE_ALL);

    Target->LastModified = OSXGetLastModifiedTime(DAIS_INUSE_STR);
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

