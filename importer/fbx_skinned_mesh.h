#ifndef FBX_SKINNED_MESH_H_
#define FBX_SKINNED_MESH_H_

#include "openfbx/ofbx.h"
#include "animation_types.h"

struct skinned_mesh_with_pose {
    skinned_mesh Mesh;
    skeleton_pose BindPose;
    bone_assignment BoneNames;
};

skinned_mesh_with_pose *ConvertFBXToSkinnedMesh(Options *Opts, const IScene *Scene);

bool WriteMesh(skinned_mesh_with_pose *Mesh, const char *Filename);

void FreeConvertedSkinnedMesh(skinned_mesh_with_pose *mesh);

#endif
