

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
    LocalToWorld(Skel->WorldSetupMatrices, Skel->LocalSetupMatrices, Skel->Pose.BoneParentIDs, BoneCount);
    InvertMatrices(Skel->InverseSetupMatrices, Skel->WorldSetupMatrices, BoneCount);
}

static
void UpdateMatricesFromTransforms(skeleton *Skel) {
    u32 BoneCount = Skel->Pose.BoneCount;
    TransformsToMatrices(Skel->LocalOffsets, Skel->LocalTransforms, BoneCount);
    MultiplyMatrices(Skel->LocalMatrices, Skel->WorldSetupMatrices, Skel->LocalOffsets, Skel->InverseSetupMatrices, BoneCount);
    LocalToWorld(Skel->WorldMatrices, Skel->LocalMatrices, Skel->Pose.BoneParentIDs, BoneCount);
}
