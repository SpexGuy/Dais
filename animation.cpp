

// nonstandard mat4x3 * mat4x3 -> mat4x3 multiply
template <typename T>
GLM_FUNC_DECL glm::detail::tmat4x3<T> operator* (
    glm::detail::tmat4x3<T> const & m1, 
    glm::detail::tmat4x3<T> const & m2)
{
    // OPT: This could probably be optimized,
    // inspect assembly if it matters.
    return m1 * mat4(m2);
}

static inline
mat4x3 MatrixFromTransform(transform *Trans) {
    // OPT: this is a very slow way to do this.
    mat4 Translation = glm::translate(Trans->Translation);
    mat4 Rotation = glm::mat4_cast(Trans->Rotation);
    mat4 Scale = glm::scale(Trans->Scale);
    mat4 Combined = Translation * Rotation * Scale;
    return mat4x3(Combined);
}

static
void TransformsToMatrices(
        mat4x3 * restrict Matrices,
        transform * restrict Transforms,
        u32 Count)
{
    for (u32 Index = 0; Index < Count; Index++) {
        Matrices[Index] = MatrixFromTransform(Transforms + Index);
    }
}

static
void MultiplyMatrices(
    mat4x3 * restrict Results,
    mat4x3 * restrict As,
    mat4x3 * restrict Bs,
    u32 Count)
{
    for (u32 Index = 0; Index < Count; Index++) {
        Results[Index] = As[Index] * Bs[Index];
    }
}

static
void MultiplyMatrices(
    mat4x3 * restrict Results,
    mat4x3 * restrict As,
    mat4x3 * restrict Bs,
    mat4x3 * restrict Cs,
    u32 Count)
{
    for (u32 Index = 0; Index < Count; Index++) {
        Results[Index] = As[Index] * Bs[Index] * Cs[Index];
    }
}

static
void InvertMatrices(
        mat4x3 * restrict Inverted,
        mat4x3 * restrict Source,
        u32 Count)
{
    for (u32 Index = 0; Index < Count; Index++) {
        Inverted[Index] = mat4x3(glm::inverse(mat4(Source[Index])));
        // mat3 InverseRotScale = glm::inverse(mat3(Source[Index]));
        // vec3 InverseTranslation = -Source[Index][3];
        // Inverted[Index] = mat4x3(
        //     InverseRotScale[0],
        //     InverseRotScale[1],
        //     InverseRotScale[2],
        //     InverseTranslation);
    }
}

static
void LocalToWorld(
        mat4x3 * restrict World,
        mat4x3 * restrict Local,
        u16 * restrict Parents,
        u32 Count)
{
    Assert(Count > 0);
    World[0] = Local[0];
    for (u32 Index = 1; Index < Count; Index++) {
        u32 Parent = Parents[Index];
        Assert(Parent < Index);
        World[Index] = World[Parent] * Local[Index];
    }
}

static
void UpdateSetupMatrices(skeleton *Skel) {
    u32 BoneCount = Skel->Pose.BoneCount;
    TransformsToMatrices(Skel->LocalSetupMatrices, Skel->Pose.SetupPose, BoneCount);
    InvertMatrices(Skel->InverseLocalSetupMatrices, Skel->LocalSetupMatrices, BoneCount);
    LocalToWorld(Skel->WorldSetupMatrices, Skel->LocalSetupMatrices, Skel->Pose.BoneParentIDs, BoneCount);
    InvertMatrices(Skel->InverseSetupMatrices, Skel->WorldSetupMatrices, BoneCount);
}

static
void UpdateMatricesFromTransforms(skeleton *Skel) {
    u32 BoneCount = Skel->Pose.BoneCount;
    TransformsToMatrices(Skel->LocalMatrices, Skel->LocalTransforms, BoneCount);
    MultiplyMatrices(Skel->LocalOffsets, Skel->InverseLocalSetupMatrices, Skel->LocalMatrices, BoneCount);
    MultiplyMatrices(Skel->LocalMatrices, Skel->WorldSetupMatrices, Skel->LocalOffsets, Skel->InverseSetupMatrices, BoneCount);
    LocalToWorld(Skel->WorldMatrices, Skel->LocalMatrices, Skel->Pose.BoneParentIDs, BoneCount);
}

static
u32 BinarySearchLower(f32 *Values, u32 Count, f32 Target) {
    Assert(Count >= 2);

    // TODO: fix the importer to avoid these cases
    if (Values[0] >= Target) return 0;
    if (Values[Count-1] <= Target) return Count-2;

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

static inline
vec3 Mix(vec3 &From, vec3 &To, f32 RawInterp) {
    return glm::mix(From, To, RawInterp);
}

static inline
quat Mix(quat &From, quat &To, f32 RawInterp) {
    // Approximates slerp for small angles
    // Does not normalize the resulting quaternion
    // As long as the quaternions are close together, this is fine.
    f32 Cosom = glm::dot(From, To);
    f32 Scale0 = 1.0f - RawInterp;
    f32 Scale1 = copysignf(RawInterp, Cosom);
    return Scale0 * From + Scale1 * To;
}

template <typename pt>
static
pt LookupAtPercent(timeline<pt> &Timeline, f32 Percent) {
    Assert(Timeline.KeyframeCount > 0);

    // TODO: Fix the importer to avoid this case
    if (Timeline.KeyframeCount == 1) return Timeline.Values[0];

    int Index = BinarySearchLower(Timeline.Percentages, Timeline.KeyframeCount, Percent);
    f32 Lowp = Timeline.Percentages[Index];
    f32 Highp = Timeline.Percentages[Index+1];
    f32 RawInterp = glm::clamp((Percent - Lowp) / (Highp - Lowp), 0.0f, 1.0f);
    pt &From = Timeline.Values[Index];
    pt &To = Timeline.Values[Index+1];
    pt Result = Mix(From, To, RawInterp);
    return Result;
}

static
void SetAnimationToPercent(skeleton *Skel, animation *Anim, float Percent) {
    for (u32 BoneIndex = 0; BoneIndex < Anim->AnimatedBoneCount; BoneIndex++) {
        bone_animation *BoneAnim = Anim->Bones + BoneIndex;
        u32 BoneID = BoneAnim->BoneID;
        Assert(BoneID < Skel->Pose.BoneCount);
        transform *LocalTransform = Skel->LocalTransforms + BoneID;
        if (BoneAnim->ChannelFlags & CHANNEL_FLAG_TRANSLATION) {
            LocalTransform->Translation = LookupAtPercent(BoneAnim->Translations, Percent);
        }
        if (BoneAnim->ChannelFlags & CHANNEL_FLAG_ROTATION) {
            LocalTransform->Rotation = LookupAtPercent(BoneAnim->Rotations, Percent);
        }
        if (BoneAnim->ChannelFlags & CHANNEL_FLAG_SCALE) {
            LocalTransform->Scale = LookupAtPercent(BoneAnim->Scales, Percent);
        }
    }
}

static
animation *LoadAnimation(memory_arena *Arena, void *FileData) {
    u8 *FileBase = (u8 *) FileData;
    u8 *FilePos = FileBase;

    u32 PercentStart = *(u32 *) FilePos;
    FilePos += sizeof(u32);
    u32 DataStart = *(u32 *) FilePos;
    FilePos += sizeof(u32);

    float *PercentPos = (float *) (FileBase + PercentStart);
    float *DataPos = (float *) (FileBase + DataStart);

    animation *Anim = ArenaAllocT(Arena, animation);
    Anim->Duration = *(f32*)FilePos;
    FilePos += sizeof(f32);
    Anim->AnimatedBoneCount = *(u16*)FilePos;
    FilePos += sizeof(u16) * 2; // pad here to align

    Anim->Bones = ArenaAllocTN(Arena, bone_animation, Anim->AnimatedBoneCount);
    for (u32 BoneIndex = 0; BoneIndex < Anim->AnimatedBoneCount; BoneIndex++) {
        bone_animation *Bone = Anim->Bones + BoneIndex;
        struct {
            u16 BoneID;
            u16 TranslationCount;
            u16 RotationCount;
            u16 ScaleCount;
        } BoneData;
        memcpy(&BoneData, FilePos, sizeof(BoneData));
        FilePos += sizeof(BoneData);
        Bone->BoneID = BoneData.BoneID;
        Bone->ChannelFlags = 0;
        Bone->Translations.KeyframeCount = BoneData.TranslationCount;
        if (BoneData.TranslationCount) {
            Bone->ChannelFlags |= CHANNEL_FLAG_TRANSLATION;
            Bone->Translations.Percentages = PercentPos;
            PercentPos += BoneData.TranslationCount;
            Bone->Translations.Values = (vec3 *) DataPos;
            DataPos += BoneData.TranslationCount * 3;
        }
        Bone->Rotations.KeyframeCount = BoneData.RotationCount;
        if (BoneData.RotationCount) {
            Bone->ChannelFlags |= CHANNEL_FLAG_ROTATION;
            Bone->Rotations.Percentages = PercentPos;
            PercentPos += BoneData.RotationCount;
            Bone->Rotations.Values = (quat *) DataPos;
            DataPos += BoneData.RotationCount * 4;
        }
        Bone->Scales.KeyframeCount = BoneData.ScaleCount;
        if (BoneData.ScaleCount) {
            Bone->ChannelFlags |= CHANNEL_FLAG_SCALE;
            Bone->Scales.Percentages = PercentPos;
            PercentPos += BoneData.ScaleCount;
            Bone->Scales.Values = (vec3 *) DataPos;
            DataPos += BoneData.ScaleCount * 3;
        }
    }

    // for (u32 BoneIndex = 0; BoneIndex < Anim->AnimatedBoneCount; BoneIndex++) {
    //     bone_animation *Bone = Anim->Bones + BoneIndex;

    //     printf("Bone %d\n", Bone->BoneID);
    //     for (u32 c = 0; c < Bone->Translations.KeyframeCount; c++) {
    //         printf("T %f [%f %f %f]\n",
    //             Bone->Translations.Percentages[c],
    //             Bone->Translations.Values[c].x,
    //             Bone->Translations.Values[c].y,
    //             Bone->Translations.Values[c].z);
    //     }
    //     for (u32 c = 0; c < Bone->Rotations.KeyframeCount; c++) {
    //         printf("R %f [%f %f %f %f]\n",
    //             Bone->Rotations.Percentages[c],
    //             Bone->Rotations.Values[c].x,
    //             Bone->Rotations.Values[c].y,
    //             Bone->Rotations.Values[c].z,
    //             Bone->Rotations.Values[c].w);
    //     }
    //     for (u32 c = 0; c < Bone->Scales.KeyframeCount; c++) {
    //         printf("S %f [%f %f %f]\n",
    //             Bone->Scales.Percentages[c],
    //             Bone->Scales.Values[c].x,
    //             Bone->Scales.Values[c].y,
    //             Bone->Scales.Values[c].z);
    //     }
    //     printf("\n");
    // }

    return Anim;
}
