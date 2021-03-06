#ifndef ANIMATION_TYPES_H_
#define ANIMATION_TYPES_H_

// Arbitrarily picked. Could probably adjust this for tuning.
#define MAX_BONE_IDS_PER_DRAW 12

#define CHANNEL_FLAG_TRANSLATION (1<<0)
#define CHANNEL_FLAG_ROTATION (1<<1)
#define CHANNEL_FLAG_SCALE (1<<2)

struct transform {
    vec3 Translation;
    quat Rotation;
    vec3 Scale;
};

template <typename pt>
struct timeline {
    u32 KeyframeCount;
    // sorted ASC
    f32 *Percentages;
    // index matches Percentages
    pt *Values;
};

struct bone_animation {
    u16 BoneID;
    u16 ChannelFlags;
    timeline<vec3> Translations;
    timeline<quat> Rotations;
    timeline<vec3> Scales;
};

struct animation {
    f32 Duration;
    u16 AnimatedBoneCount;
    // sorted by BoneID ASC
    bone_animation *Bones;
};

struct skeleton_pose {
    u16 BoneCount;
    // BoneParentIDs[c] < c, except BoneParentIDs[0] == 0
    u16 *BoneParentIDs;
    transform *SetupPose;
};

struct skeleton {
    skeleton_pose Pose;
    mat4x3 *LocalSetupMatrices;
    mat4x3 *InverseLocalSetupMatrices;
    mat4x3 *WorldSetupMatrices;
    mat4x3 *InverseSetupMatrices;
    transform *LocalTransforms;
    mat4x3 *LocalOffsets;
    mat4x3 *LocalMatrices;
    mat4x3 *CompositeMatrices;
    mat4x3 *WorldMatrices;
};

struct skinned_mesh_mesh {
    u16 VertexCount;
    u16 IndexCount; // may be zero
    u16 VertexSize;
    u16 BoneCount;
    f32 *VertexData;
    u16 *IndexData;
    u32 VaoID;
    u32 BufferIDs[2];
};

struct skinned_mesh_draw {
    u16 MaterialID;
    u16 MeshID;
    u16 MeshOffset; // first vertex
    u16 MeshLength; // number of vertices
    u16 ParentBoneID;
    u16 NumBoneIDs;
    u16 BoneIDs[MAX_BONE_IDS_PER_DRAW];
};

struct texture {
    char *TexturePath;
    u32 GLTexID;
};

// this struct is compared with memcmp, so it's important that
// it has no padding.
struct material {
    b32 LambertOnly;
    f32 Ambient[3];
    f32 Diffuse[3];
    f32 Specular[3];
    f32 Emissive[3];
    f32 Shininess;
    f32 Opacity;
    u16 DiffuseTexID;
    u16 NormalTexID;
};

struct skinned_mesh {
    u16 DrawCount;
    u16 MeshCount;
    u16 MaterialCount;
    u16 TextureCount;

    skinned_mesh_draw *Draws;
    skinned_mesh_mesh *Meshes;
    material *Materials;
    texture *Textures;

    skeleton_pose BindPose;
};

static inline
bool operator==(const material &a, const material &b) {
    return memcmp(&a, &b, sizeof(material)) == 0;
}

#endif
