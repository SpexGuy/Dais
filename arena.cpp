struct memory_arena {
    char *Base;
    u32 Pos;
    u32 Capacity;
};

void ArenaInit(memory_arena *Arena, void *Base, u32 Capacity) {
    Arena->Base = (char *) Base;
    Arena->Pos = 0;
    Arena->Capacity = Capacity;
}

void ArenaAlign(memory_arena *Arena, u32 Align) {
    Assert(IsPowerOfTwo(Align));
    uptr Pos = (uptr) (Arena->Base + Arena->Pos);
    Pos = AlignRoundUp(Pos, (uptr) Align);
    Arena->Pos = (u32)(Pos - (uptr)(Arena->Base));
}

void *ArenaAlloc(memory_arena *Arena, u32 Size) {
    Assert(Arena->Pos + Size > Arena->Pos); // overflow check
    Assert(Arena->Pos + Size <= Arena->Capacity); // bounds check
    char *Alloc = Arena->Base + Arena->Pos;
    Arena->Pos += Size;
    return Alloc;
}

void *ArenaAlloc(memory_arena *Arena, u32 Size, u32 Count) {
    return ArenaAlloc(Arena, Size * Count);
}

void ArenaClear(memory_arena *Arena) {
    memset(Arena->Base, 0, Arena->Pos);
    Arena->Pos = 0;
}

void ArenaRestore(memory_arena *Arena, u32 Pos) {
    if (Pos < Arena->Pos) {
        memset(Arena->Base + Pos, 0, Arena->Pos - Pos);
        Arena->Pos = Pos;
    }
}

void *ArenaCopy(memory_arena *Arena, const void *Ptr, u32 Size) {
    void *Mem = ArenaAlloc(Arena, Size);
    memcpy(Mem, Ptr, Size);
    return Mem;
}

char *ArenaStrcpy(memory_arena *Arena, const char *Str) {
    u32 Len = strlen(Str);
    return (char *) ArenaCopy(Arena, Str, Len+1);
}

#define ArenaAllocT(ARENA, TYPE) \
    ((TYPE *) ArenaAlloc(ARENA, sizeof(TYPE)))
#define ArenaAllocTN(ARENA, TYPE, COUNT) \
    ((TYPE *) ArenaAlloc(ARENA, sizeof(TYPE) * (COUNT)))

#define ArenaCopyT(ARENA, VALUE, TYPE) \
    ((TYPE *) ArenaCopy(ARENA, VALUE, sizeof(TYPE)))
#define ArenaCopyTN(ARENA, VALUE, TYPE, COUNT) \
    ((TYPE *) ArenaCopy(ARENA, VALUE, sizeof(TYPE) * (COUNT)))

