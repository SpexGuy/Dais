
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
    Arena->Pos = 0;
}

