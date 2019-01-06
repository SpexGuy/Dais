#include "common_macros.h"
#include "animation_types.h"

v3 Lerp(v3 &a, v3 &b, f32 z) {
    v3 Result;

    Result.x = a.x + (b.x - a.x) * z;
    Result.y = a.y + (b.y - a.y) * z;
    Result.z = a.z + (b.z - a.z) * z;

    return Result;
}

void Normalize(quat &q) {
    f32 len = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    f32 ilen = 1.0f / len;
    q.x *= ilen;
    q.y *= ilen;
    q.z *= ilen;
    q.w *= ilen;
}

quat Lerp(quat &a, quat &b, f32 z) {
    quat Result;

    Result.x = a.x + (b.x - a.x) * z;
    Result.y = a.y + (b.y - a.y) * z;
    Result.z = a.z + (b.z - a.z) * z;
    Result.w = a.w + (b.w - a.w) * z;

    Normalize(Result);

    return Result;
}

u32 BinarySearchLower(f32 *Values, u32 Count, f32 Target) {
    Assert(Count >= 2);
    Assert(Values[0] <= Target);
    Assert(Values[Count-1] >= Target);

    u32 Low = 0;
    u32 High = Count-1;

    // TODO exercise: experiment with going branchless within the loop
    while (Low + 1 < High) {
        u32 Mid = (Low + High) / 2;
        if (Values[Mid] <= Target) {
            Low = Mid;
        } else {
            High = Mid;
        }
    }

    Assert(Low + 1 == High);
    Assert(High < Count);
    Assert(High != 0);

    return Low;
}

template <typename pt>
pt LookupAtPercent(timeline<pt> &Timeline, f32 Percent) {
    int Index = BinarySearchLower(Timeline.Percentages, Timeline.KeyframeCount, Percent);
    f32 Lowp = Timeline.Percentages[Index];
    f32 Highp = Timeline.Percentages[Index+1];
    f32 RawInterp = (Percent - Lowp) / (Highp - Lowp);
    pt &From = Timeline.Values[Index];
    pt &To = Timeline.Values[Index+1];
    pt Result = Lerp(From, To, RawInterp);
    return Result;
}

