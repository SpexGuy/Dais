
static
char *TPrintf(const char *Format, ...) {
    char *Value;
    va_list Argptr;
    va_start(Argptr, Format);
    Value = ArenaVPrintf(TempArena, Format, Argptr);
    va_end(Argptr);
    return Value;
}

static
char *TCat(const char *A, const char *B) {
    char *Result;
    u32 LenA = strlen(A);
    u32 LenB = strlen(B);
    Result = ArenaAllocTN(TempArena, char, LenA + LenB + 1);
    memcpy(Result, A, LenA);
    memcpy(Result + LenA, B, LenB);
    Result[LenA + LenB] = '\0';
    return Result;
}
