struct memory_arena {
    char *Base;
    u32 Pos;
    u32 Capacity;
};

static inline
void ArenaInit(memory_arena *Arena, void *Base, u32 Capacity) {
    Arena->Base = (char *) Base;
    Arena->Pos = 0;
    Arena->Capacity = Capacity;
}

static inline
void ArenaAlign(memory_arena *Arena, u32 Align) {
    Assert(IsPowerOfTwo(Align));
    uptr Pos = (uptr) (Arena->Base + Arena->Pos);
    Pos = AlignRoundUp(Pos, (uptr) Align);
    Arena->Pos = (u32)(Pos - (uptr)(Arena->Base));
}

static
void *ArenaAlloc(memory_arena *Arena, u32 Size) {
    Assert(Arena->Pos + Size >= Arena->Pos); // overflow check
    Assert(Arena->Pos + Size <= Arena->Capacity); // bounds check
    char *Alloc = Arena->Base + Arena->Pos;
    Arena->Pos += Size;
    return Alloc;
}

static inline
void *ArenaAlloc(memory_arena *Arena, u32 Size, u32 Count) {
    return ArenaAlloc(Arena, Size * Count);
}

static inline
void ArenaClear(memory_arena *Arena) {
    memset(Arena->Base, 0, Arena->Pos);
    Arena->Pos = 0;
}

static
void ArenaRestore(memory_arena *Arena, u32 Pos) {
    if (Pos < Arena->Pos) {
        memset(Arena->Base + Pos, 0, Arena->Pos - Pos);
        Arena->Pos = Pos;
    }
}

static
void *ArenaCopy(memory_arena *Arena, const void *Ptr, u32 Size) {
    void *Mem = ArenaAlloc(Arena, Size);
    memcpy(Mem, Ptr, Size);
    return Mem;
}

static
char *ArenaStrcpy(memory_arena *Arena, const char *Str) {
    u32 Len = strlen(Str);
    return (char *) ArenaCopy(Arena, Str, Len+1);
}

static
char *ArenaVPrintf(memory_arena *Arena, const char *Format, va_list Argptr) {
    char *Base = Arena->Base + Arena->Pos;
    u32 Limit = Arena->Capacity - Arena->Pos;
    u32 Printed = vsnprintf(Base, Limit - 1, Format, Argptr);
    Arena->Pos += Printed + 1;
    Assert(Arena->Pos < Arena->Capacity);
    return Base;
}

static
char *ArenaPrintf(memory_arena *Arena, const char *Format, ...) {
    char *Value;
    va_list Argptr;
    va_start(Argptr, Format);
    Value = ArenaVPrintf(Arena, Format, Argptr);
    va_end(Argptr);
    return Value;
}

#define ArenaAllocT(ARENA, TYPE) \
    ((TYPE *) ArenaAlloc(ARENA, sizeof(TYPE)))
#define ArenaAllocTN(ARENA, TYPE, COUNT) \
    ((TYPE *) ArenaAlloc(ARENA, sizeof(TYPE) * (COUNT)))

#define ArenaCopyT(ARENA, VALUE, TYPE) \
    ((TYPE *) ArenaCopy(ARENA, VALUE, sizeof(TYPE)))
#define ArenaCopyTN(ARENA, VALUE, TYPE, COUNT) \
    ((TYPE *) ArenaCopy(ARENA, VALUE, sizeof(TYPE) * (COUNT)))

