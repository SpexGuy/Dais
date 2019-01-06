#include "fbx_skinned_mesh.h"
#include "mathutil.h"

#define TMP_MEM_SIZE Megabytes(16)

// position, normal, uv, index
#define MAX_VERTEX_SIZE_FLOATS (3 + 3 + 2 + 2*MAX_BLEND_WEIGHTS)

struct blend_weight {
    s32 BoneIndex;
    f32 Weight;
};

struct pre_mesh_part {
    pre_mesh_part *Next;
    s32 BoneIDs[MAX_DRAW_BONES];
    u16 MaterialID;
};

struct limb_node_info {
    limb_node_info *Next;
    u16 ID;
    u16 ParentID;
    const Object *Source;
};

struct mesh_node_info {
    mesh_node_info *Next;
    u16 ParentID;
    const Mesh *Source;

    u32 VertexCount;
    const Vec3 *Positions;
    const Vec2 *UVs;
    const int *Materials;
    const Skin *FbxSkin;

    int BlendWeightCount;
    blend_weight *BlendWeights;

    int MaterialCount;
    u16 *MatIDs;

    u16 *TrisToParts;
    int DrawBoneCount;

    u32 MeshPartsCount;
    pre_mesh_part *MeshParts;
};

struct mesh_type {
    mesh_type *Next;
    int BlendWeightCount;
    mesh_node_info *Nodes;
};

struct texture_info {
    texture_info *Next;
    char *File;
};

struct material_info {
    material_info *Next;
    material Mat;
};

struct skinning_state {
    memory_arena Arena;
    skinned_mesh *Mesh;
    bone_assignment *BoneNames;
    Options *Opts;
    u16 NextNodeID;
    limb_node_info *Limbs;
    mesh_node_info *Meshes;
    texture_info *Textures;
    material_info *Materials;

    u16 BoneCount;

    mesh_type *MeshTypes;

    u32 DrawIndex;
};

const int bufsize = MAX_NODE_NAME_LENGTH;

static
u32 ReverseLinkedList(void **List) {
    u32 Size = 0;

    void **Curr = (void **) *List;
    void **Reversed = 0;
    while (Curr) {
        Size++;
        void **Next = (void **) *Curr;
        *Curr = Reversed;        Reversed = Curr;
        Curr = Next;
    }
    *List = Reversed;

    return Size;
}

static
u32 CountLinkedList(void *List) {
    u32 Size = 0;

    void **Curr = (void **) List;
    while (Curr) {
        Size++;
        Curr = (void **) *Curr;
    }

    return Size;
}

static
const IElementProperty *FindNonemptyStringProperty(const IElementProperty *Prop, char(&Buf)[bufsize]) {
    while (Prop) {
        if (Prop->getType() == IElementProperty::STRING) {
            Prop->getValue().toString(Buf);
            if (Buf[0]) break;
        }
        Prop = Prop->getNext();
    }
    return Prop;
}

static
const IElementProperty *FindDoubleProperty(const IElementProperty *Prop) {
    while (Prop) {
        if (Prop->getType() == IElementProperty::DOUBLE) {
            break;
        }
        Prop = Prop->getNext();
    }
    return Prop;
}

// attempts to get n floats from prop to put in buf, returns actual number retrieved.
static
u32 GetFloats(f32 *Buf, u32 Num, const IElementProperty *Prop) {
    u32 c = 0;
    Prop = FindDoubleProperty(Prop);
    while (c < Num && Prop) {
        Buf[c++] = (f32) Prop->getValue().toDouble();
        Prop = FindDoubleProperty(Prop->getNext());
    }
    return c;
}

u16 FindBoneIDByName(bone_assignment *Assignment, const char *Name) {
    Assert(Assignment->Names);
    for (u16 BoneID = 0; BoneID < Assignment->BoneCount; BoneID++) {
        if (strncmp(Name, Assignment->Names[BoneID].Name, MAX_NODE_NAME_LENGTH) == 0) {
            return BoneID;
        }
    }

    printf("Couldn't find bone with name %s\n", Name);
    Assert(false);
    return 0;
}



// ---------------------- Blend Weights ------------------------

static
int ComputeBlendWeightCount(skinning_state *State, const Skin *FbxSkin, int VertexCount, int MaxBlendWeights) {
    u32 StartPos = State->Arena.Pos;
    int *RefCounts = ArenaAllocTN(&State->Arena, int, VertexCount);
    int ClusterCount = FbxSkin->getClusterCount();
    for (int c = 0; c < ClusterCount; c++) {
        const Cluster *FbxCluster = FbxSkin->getCluster(c);
        // not sure why these are separate, they seem to always be the same. Min just in case.
        int WeightsCount = FbxCluster->getWeightsCount();
        int IndicesCount = FbxCluster->getIndicesCount();
        Assert(WeightsCount == IndicesCount);
        int PointsCount = Min(WeightsCount, IndicesCount);

        const int *Indices = FbxCluster->getIndices();
        const double *Weights = FbxCluster->getWeights();
        for (int d = 0; d < PointsCount; d++) {
            u32 Index = (u32) Indices[d];
            if (Index < VertexCount && Weights[d] != 0) {
                RefCounts[Index]++;
            }
        }
    }

    int MaxCount = 0;
    for (int c = 0; c < VertexCount; c++) {
        SetMax(MaxCount, RefCounts[c]);
    }

    if (MaxCount > MaxBlendWeights) {
        printf("Truncating number of blend weights from %d -> %d\n", MaxCount, MaxBlendWeights);
        MaxCount = MaxBlendWeights;
    }

    // dealloc RefCounts
    ArenaRestore(&State->Arena, StartPos);

    return MaxCount;
}

static inline
void NormalizeBlendWeights(blend_weight *Weights, int NumWeights) {
    float Sum = 0;
    for (int d = 0; d < NumWeights; d++) {
        Sum += Weights[d].Weight;
    }
    if (Sum != 0) {
        for (int d = 0; d < NumWeights; d++) {
            Weights[d].Weight /= Sum;
        }
    }
}

static
blend_weight *ComputeBlendWeights(skinning_state *State, const Skin *FbxSkin, int MaxWeights, int VertexCount) {
    int TotalWeights = MaxWeights * VertexCount;
    blend_weight *Data = ArenaAllocTN(&State->Arena, blend_weight, TotalWeights);

    f32 MaxDisplacedWeight = 0;
    int ClusterCount = FbxSkin->getClusterCount();
    for (int c = 0; c < ClusterCount; c++) {
        const Cluster *FbxCluster = FbxSkin->getCluster(c);
        // not sure why these are separate, they seem to always be the same. Min just in case.
        // not sure why these are separate, they seem to always be the same. Min just in case.
        int WeightsCount = FbxCluster->getWeightsCount();
        int IndicesCount = FbxCluster->getIndicesCount();
        Assert(WeightsCount == IndicesCount);
        int PointsCount = Min(WeightsCount, IndicesCount);

        const Object *Link = FbxCluster->getLink();
        Assert(Link->getType() == Object::Type::LIMB_NODE);
        u16 BoneID = FindBoneIDByName(State->BoneNames, Link->name);

        const int *Indices = FbxCluster->getIndices();
        const double *Weights = FbxCluster->getWeights();
        for (int d = 0; d < PointsCount; d++) {
            u32 Index = (u32) Indices[d];
            if (Index >= VertexCount) continue;

            f32 Weight = (f32) Weights[d];
            if (Weight < 0.00001) continue;

            blend_weight *VertWeights = &Data[Index * MaxWeights];
            // find the minimum current weight
            int MinWeightIdx = 0;
            for (int e = 1; e < MaxWeights; e++) {
                if (fabs(VertWeights[e].Weight) < fabs(VertWeights[MinWeightIdx].Weight)) {
                    MinWeightIdx = e;
                }
            }

            // replace with the new weight if greater
            if (fabs(Weight) > fabs(VertWeights[MinWeightIdx].Weight)) {
                SetMax(MaxDisplacedWeight, fabs(VertWeights[MinWeightIdx].Weight));
                VertWeights[MinWeightIdx].Weight = Weight;
                VertWeights[MinWeightIdx].BoneIndex = BoneID;
            }
        }
    }

    if (MaxDisplacedWeight != 0) {
        printf("Displaced a maximum vertex weight of %f\n", MaxDisplacedWeight);
    }

    // normalize weights
    blend_weight *VertWeights = Data;
    for (int c = 0; c < VertexCount; c++, VertWeights += MaxWeights) {
        NormalizeBlendWeights(VertWeights, MaxWeights);
    }

    return Data;
}

static
int PolyCompare(const void *a, const void *b) {
    blend_weight *ba = (blend_weight *) a;
    blend_weight *bb = (blend_weight *) b;
    f32 Result = bb->Weight - ba->Weight;
    return Result < 0 ? -1 : 1;
}

static
u16 FindOrCreateMeshPart(skinning_state *State, mesh_node_info *Mesh, int MaterialID, blend_weight *Weights, int NumWeights, int DrawBoneCount) {
    // build a list of the required nodes for this poly
    const u32 PolyNodesCount = MAX_BLEND_WEIGHTS * 3;
    blend_weight PolyNodes[PolyNodesCount] = {};
    for (u32 c = 0; c < PolyNodesCount; c++) {
        PolyNodes[c].BoneIndex = -1;
    }

    int TotalPolyWeights = NumWeights * 3;
    for (int w = 0; w < TotalPolyWeights; w++) {
        blend_weight &Weight = Weights[w];
        int Idx = Weight.BoneIndex;
        if (Idx < 0) continue;

        // find or add the node id
        u32 i = 0;
        while (PolyNodes[i].BoneIndex >= 0 && PolyNodes[i].BoneIndex != Idx) i++;
        Assert(i < TotalPolyWeights);
        PolyNodes[i].BoneIndex = Idx;
        PolyNodes[i].Weight += Weight.Weight;
    }

    int MaxIdx = 0;
    while (MaxIdx < PolyNodesCount && PolyNodes[MaxIdx].BoneIndex >= 0) MaxIdx++;

    // If we have more bones in this poly than DrawBoneCount, we need to cull some.
    if (MaxIdx > DrawBoneCount) {
        qsort(PolyNodes, MaxIdx, sizeof(blend_weight), PolyCompare);
        // zero any weights we've removed
        for (int c = DrawBoneCount; c < MaxIdx; c++) {
            blend_weight &PolyWeight = PolyNodes[c];
            for (int w = 0; w < TotalPolyWeights; w++) {
                blend_weight &VertexWeight = Weights[w];
                if (VertexWeight.BoneIndex == PolyWeight.BoneIndex) {
                    VertexWeight.BoneIndex = -1;
                    VertexWeight.Weight = 0;
                }
            }
        }
        // renormalize the blend weights on our vertices
        NormalizeBlendWeights(Weights + 0*NumWeights, NumWeights);
        NormalizeBlendWeights(Weights + 1*NumWeights, NumWeights);
        NormalizeBlendWeights(Weights + 2*NumWeights, NumWeights);

        MaxIdx = DrawBoneCount;
    }

    // Find an existing mesh part that can accept this set of Weights and material
    u16 PartID = 0;
    pre_mesh_part **PPart = &Mesh->MeshParts;
    while (1) {
        pre_mesh_part *Part = *PPart;
        if (!Part) {
            // no existing part, make a new one
            Mesh->MeshPartsCount++;
            pre_mesh_part *NewPart = ArenaAllocT(&State->Arena, pre_mesh_part);
            memset(NewPart->BoneIDs, -1, sizeof(NewPart->BoneIDs));
            NewPart->MaterialID = MaterialID;
            for (int c = 0; c < MaxIdx; c++) {
                NewPart->BoneIDs[c] = PolyNodes[c].BoneIndex;
            }
            *PPart = NewPart;
            break;
        }

        if (Part->MaterialID == MaterialID) {
            s32 CombinedBoneIDs[MAX_DRAW_BONES];
            Assert(sizeof(Part->BoneIDs) == sizeof(CombinedBoneIDs));
            memcpy(CombinedBoneIDs, Part->BoneIDs, sizeof(CombinedBoneIDs));

            for (int c = 0; c < MaxIdx; c++) {
                blend_weight &PolyNode = PolyNodes[c];
                int i = 0;
                while (i < DrawBoneCount && CombinedBoneIDs[i] >= 0 && CombinedBoneIDs[i] != PolyNode.BoneIndex) i++;
                if (i >= DrawBoneCount) goto next_part;
                CombinedBoneIDs[i] = PolyNode.BoneIndex;
            }

            // If we get here, we successfully combined all the attributes!
            memcpy(Part->BoneIDs, CombinedBoneIDs, sizeof(CombinedBoneIDs));
            break;
        }

    next_part:
        PartID++;
        PPart = &Part->Next;
    }

    return PartID;
}

static
u16 *AssignTrisToPartsFromSkin(skinning_state *State, mesh_node_info *Mesh) {
    // TODO: This is a really nasty optimization problem that I'm going to ignore for now.
    // The problem is as follows:
    // We have a bunch of polygons; each polygon has a set of up to nDrawBones bones on which it depends.
    // We'd like to make a bunch of mesh parts. Each part has nVertWeight bones which it has available.
    // Find the minimum set of mesh parts such that the bones for any polygon are a subset of one of the parts.

    // My dumb greedy solution:
    // For each polygon,
    //   Look through all existing mesh parts for one in which the size of the union of the mesh part's bones and the polygon's bones is less than nVertWeights
    //   If there is such a mesh part, add any missing bones to it and assign the polygon to that part.
    //   Otherwise, make a new mesh part, set its bones to the polygon's bones, and assign the polygon to that part.

    int VertexCount = Mesh->VertexCount;
    int BlendWeightCount = Mesh->BlendWeightCount;
    int DrawBoneCount = Mesh->DrawBoneCount;
    blend_weight *BlendWeights = Mesh->BlendWeights;
    const int *Materials = Mesh->Materials; // may be null

    Assert(VertexCount % 3 == 0);
    int TriCount = VertexCount / 3;
    u16 *TrisToParts = ArenaAllocTN(&State->Arena, u16, TriCount);
    for (int c = 0; c < TriCount; c++) {
        int MaterialIndex = 0;
        // find the material
        if (Materials) {
            MaterialIndex = *Materials;
        }

        Assert(MaterialIndex < Mesh->MaterialCount);
        u16 MaterialID = Mesh->MatIDs[MaterialIndex];

        TrisToParts[c] = FindOrCreateMeshPart(State, Mesh, MaterialID, BlendWeights, BlendWeightCount, DrawBoneCount);

        // move forward one triangle
        BlendWeights += BlendWeightCount * 3;
        if (Materials) Materials++;
    }

    printf("Packed %d triangles into %d mesh parts not exceeding %d bones.\n",
        TriCount, CountLinkedList(Mesh->MeshParts), DrawBoneCount);
    return TrisToParts;
}

// static void addBones(MeshData *data, PreMeshPart *part, NodePart *np, const Matrix *geometry) {
//     if (part->nodes[0] < 0) return; // no bones
//     const Skin *skin = data->skin;
//     if (!skin) return;

//     int maxBones = data->nDrawBones;
//     int nBonesUsed = 0;
//     while (nBonesUsed < maxBones && part->nodes[nBonesUsed] >= 0) nBonesUsed++;

//     np->bones.resize(nBonesUsed);
//     for (int c = 0; c < nBonesUsed; c++) {
//         BoneBinding *bone = &np->bones[c];
//         int clusterIndex = part->nodes[c];
//         const Cluster *cluster = skin->getCluster(clusterIndex);
//         const Object *link = cluster->getLink(); // the node which represents this bone
//         assert(link->isNode());
//         findName(link, "Node", bone->nodeID);

//         // calculate the inverse bind pose
//         // This is pretty much a total guess, but it produces the same results as the reference converter.
//         Matrix clusterLinkTransform = cluster->getTransformLinkMatrix();
//         Matrix invLinkTransform;
//         invertMatrix(&clusterLinkTransform, &invLinkTransform);
//         Matrix bindPose = mul(&invLinkTransform, geometry);
//         Matrix invBindPose;
//         invertMatrix(&bindPose, &invBindPose);
//         extractTransform(&invBindPose, bone->translation, bone->rotation, bone->scale);
//     }
// }



// ---------------------- Materials ------------------------

// There are two ways to set each of these properties (WLOG we use Ambient as an example):
// 1) specify AmbientColor and, optionally, AmbientFactor
// 2) specify Ambient, which is AmbientColor * AmbientFactor
// The problem is that some exporters (including Maya 2016) export all three attributes.
// If the order is AmbientColor, Ambient, AmbientFactor, the factor will effectively be multiplied twice.
// To avoid this, we set the ML_AMBIENT flag when processing the Ambient attribute,
// and ignore the AmbientColor and AmbientFactor attributes when ML_AMBIENT is set.
const int ML_EMISSIVE = 1<<0;
const int ML_AMBIENT  = 1<<1;
const int ML_DIFFUSE  = 1<<2;
const int ML_SPECULAR = 1<<3;
struct material_loading {
    bool LambertOnly = false;
    int Flags = 0;

    // lambert vars
    float EmissiveColor[3] = {0,0,0};
    float EmissiveFactor = 1;
    float AmbientColor[3] = {0,0,0};
    float AmbientFactor = 1;
    float DiffuseColor[3] = {1,1,1};
    float DiffuseFactor = 1;
    float Opacity = 1;

    // phong vars
    float SpecularColor[3] = {0,0,0};
    float SpecularFactor = 1;
    float Shininess = 0;
    float ShininessExponent = 0;
};

static
void ApplyMaterialAttribute(const char *Name, const IElementProperty *Prop, material_loading *Out) {
    f32 Buf[4];

    #define LOAD_ALL(FIELD, FLAG) \
        do { if ((Out->Flags & (FLAG)) == 0) { \
            if (strcasecmp(Name, #FIELD "Color") == 0) { \
                if (GetFloats(Buf, 3, Prop) == 3) { \
                    memcpy(Out->FIELD##Color, Buf, 3 * sizeof(f32)); \
                } \
                return; \
            } else if (strcasecmp(Name, #FIELD "Factor") == 0) { \
                Prop = FindDoubleProperty(Prop); \
                if (Prop) Out->FIELD##Factor = (f32) Prop->getValue().toDouble(); \
                return; \
            } else if (strcasecmp(Name, #FIELD) == 0) { \
                if (GetFloats(Buf, 3, Prop) == 3) { \
                    memcpy(Out->FIELD##Color, Buf, 3 * sizeof(f32)); \
                    Out->FIELD##Factor = 1; \
                    Out->Flags |= (FLAG); \
                } \
                return; \
            } \
        } } while (0)

    LOAD_ALL(Ambient, ML_AMBIENT);
    LOAD_ALL(Diffuse, ML_DIFFUSE);
    LOAD_ALL(Specular, ML_SPECULAR);
    LOAD_ALL(Emissive, ML_EMISSIVE);

    if (strcasecmp(Name, "Shininess") == 0) {
        Prop = FindDoubleProperty(Prop);
        if (Prop) Out->Shininess = (f32) Prop->getValue().toDouble();
        return;
    }

    if (strcasecmp(Name, "ShininessExponent") == 0) {
        Prop = FindDoubleProperty(Prop);
        if (Prop) Out->ShininessExponent = (float) Prop->getValue().toDouble();
        return;
    }

    if (strcasecmp(Name, "Opacity") == 0) {
        Prop = FindDoubleProperty(Prop);
        if (Prop) Out->Opacity = (float) Prop->getValue().toDouble();
        return;
    }

    #undef LOAD_ALL
}

static
void ProcessMaterialNode(const IElement *Mat, material_loading *Out) {
    char Name[bufsize];
    Mat->getID().toString(Name);

    if (strcasecmp(Name, "ShadingModel") == 0) {
        const IElementProperty *Prop = Mat->getFirstProperty();
        Prop = FindNonemptyStringProperty(Prop, Name);
        if (Prop) {
            Out->LambertOnly = strcasecmp(Name, "Lambert") == 0;
        }

    } else if (strcasecmp(Name, "P") == 0) {
        const IElementProperty *Prop = Mat->getFirstProperty();
        Prop = FindNonemptyStringProperty(Prop, Name);
        if (Prop) {
            ApplyMaterialAttribute(Name, Prop, Out);
        }
    }
}

static
void ProcessMaterialNodeRecursive(const IElement *Mat, material_loading *Out) {
    while (Mat) {
        ProcessMaterialNode(Mat, Out);
        ProcessMaterialNodeRecursive(Mat->getFirstChild(), Out);
        Mat = Mat->getSibling();
    }
}

static
char *FindFilename(skinning_state *State, const Texture *Tex) {
    char Buffer[bufsize];
    Tex->getRelativeFileName().toString(Buffer);
    if (!Buffer[0]) Tex->getFileName().toString(Buffer);
    char *LastSlash = strrchr(Buffer, '/');
    char *LastBack = strrchr(Buffer, '\\');
    char *Last = LastSlash > LastBack ? LastSlash : LastBack;
    if (Last == nullptr) Last = Buffer;
    else Last++;
    char *Result = ArenaStrcpy(&State->Arena, Last);
    return Result;
}

static
u16 FindOrCreateTexture(skinning_state *State, char *File) {
    u16 TexID = 1;

    texture_info **Tex = &State->Textures;
    while (1) {
        if (!*Tex) {
            texture_info *NewTex = ArenaAllocT(&State->Arena, texture_info);
            NewTex->File = File;
            *Tex = NewTex;
            break;
        }

        if (strcmp((*Tex)->File, File) == 0) {
            break;
        }

        TexID++;

        Tex = &(*Tex)->Next;
    }

    return TexID;
}

static
u16 FindOrCreateMaterial(skinning_state *State, material *Mat) {
    u16 MatID = 1;

    material_info **Info = &State->Materials;
    while (1) {
        if (!*Info) {
            material_info *NewMat = ArenaAllocT(&State->Arena, material_info);
            NewMat->Mat = *Mat;
            *Info = NewMat;
            break;
        }

        if ((*Info)->Mat == *Mat) {
            break;
        }

        MatID++;

        Info = &(*Info)->Next;
    }

    return MatID;
}


static
void ConvertMaterial(skinning_state *State, const Material *Mat, material *Out) {
    material_loading Loading;

    const IElement *MatElm = &Mat->element;
    ProcessMaterialNode(MatElm, &Loading);
    ProcessMaterialNodeRecursive(MatElm->getFirstChild(), &Loading);

    Out->LambertOnly = Loading.LambertOnly;
    Out->Ambient[0] = Loading.AmbientColor[0] * Loading.AmbientFactor;
    Out->Ambient[1] = Loading.AmbientColor[1] * Loading.AmbientFactor;
    Out->Ambient[2] = Loading.AmbientColor[2] * Loading.AmbientFactor;
    Out->Diffuse[0] = Loading.DiffuseColor[0] * Loading.DiffuseFactor;
    Out->Diffuse[1] = Loading.DiffuseColor[1] * Loading.DiffuseFactor;
    Out->Diffuse[2] = Loading.DiffuseColor[2] * Loading.DiffuseFactor;
    Out->Emissive[0] = Loading.EmissiveColor[0] * Loading.EmissiveFactor;
    Out->Emissive[1] = Loading.EmissiveColor[1] * Loading.EmissiveFactor;
    Out->Emissive[2] = Loading.EmissiveColor[2] * Loading.EmissiveFactor;
    Out->Opacity = Loading.Opacity;
    if (!Loading.LambertOnly) {
        Out->Specular[0] = Loading.SpecularColor[0] * Loading.SpecularFactor;
        Out->Specular[1] = Loading.SpecularColor[1] * Loading.SpecularFactor;
        Out->Specular[2] = Loading.SpecularColor[2] * Loading.SpecularFactor;
        Out->Shininess = Loading.ShininessExponent == 0 ? Loading.Shininess : Loading.ShininessExponent;
    }

    const Texture *DiffuseTex = Mat->getTexture(Texture::DIFFUSE);
    if (DiffuseTex) {
        char *File = FindFilename(State, DiffuseTex);
        Out->DiffuseTexID = FindOrCreateTexture(State, File);
    }

    const Texture *NormalTex = Mat->getTexture(Texture::NORMAL);
    if (NormalTex) {
        char *File = FindFilename(State, NormalTex);
        Out->NormalTexID = FindOrCreateTexture(State, File);
    }
}

static
u16 *AssignTrisToPartsFromMaterials(skinning_state *State, mesh_node_info *Mesh) {
    int TriCount = Mesh->VertexCount / 3;
    u16 *TrisToParts = ArenaAllocTN(&State->Arena, u16, TriCount);

    for (int c = 0; c < TriCount; c++) {
        u32 MaterialIndex = (u32) Mesh->Materials[c];
        Assert(MaterialIndex < Mesh->MaterialCount);
        u16 MaterialID = Mesh->MatIDs[MaterialIndex];

        int PartID = 0;
        pre_mesh_part **PPart = &Mesh->MeshParts;
        while (1) {
            pre_mesh_part *Part = *PPart;
            if (!Part) {
                // make a new mesh part
                pre_mesh_part *NewPart = ArenaAllocT(&State->Arena, pre_mesh_part);
                NewPart->MaterialID = MaterialID;
                *PPart = NewPart;
                break;
            }

            if (Part->MaterialID == MaterialID) {
                break;
            }

            PartID++;
            PPart = &Part->Next;
        }
        TrisToParts[c] = PartID;
    }

    printf("Packed %d triangles into %d mesh parts by material\n",
        TriCount, CountLinkedList(Mesh->MeshParts));

    return TrisToParts;
}

static
void ConvertMeshNode(skinning_state *State, mesh_node_info *Info) {
    const Mesh *MeshNode = Info->Source;

    if (State->Opts->dumpMeshes) {
        dumpElement(stdout, &MeshNode->element, 7);
        dumpElementRecursive(stdout, MeshNode->element.getFirstChild(), 9);
    }

    const Geometry *Geom = MeshNode->getGeometry();
    if (State->Opts->dumpGeom) {
        dumpElement(stdout, &Geom->element, 7);
        dumpElementRecursive(stdout, Geom->element.getFirstChild(), 9);
    }

    printf("Converting mesh %s\n", MeshNode->name);

    Info->VertexCount = Geom->getVertexCount();
    Info->Positions = Geom->getVertices();
    Info->UVs = Geom->getUVs();
    Info->Materials = Geom->getMaterials();
    Info->FbxSkin = Geom->getSkin();

    int MatCount = MeshNode->getMaterialCount();
    Info->MaterialCount = Max(1, MatCount);
    Info->MatIDs = ArenaAllocTN(&State->Arena, u16, Info->MaterialCount);
    if (MatCount == 0) {
        printf("Warning: No materials for MeshNode %s. Generating default material.\n", MeshNode->name);
        material DefaultMat;
        DefaultMat.LambertOnly = true;
        Info->MatIDs[0] = FindOrCreateMaterial(State, &DefaultMat);
    } else {
        for (int c = 0; c < MatCount; c++) {
            material TmpMat;
            const Material *Mat = MeshNode->getMaterial(c);
            if (State->Opts->dumpMaterials) {
                dumpElement(stdout, &Mat->element, 7);
                dumpElementRecursive(stdout, Mat->element.getFirstChild(), 9);
            }
            ConvertMaterial(State, Mat, &TmpMat);
            Info->MatIDs[c] = FindOrCreateMaterial(State, &TmpMat);
        }
    }

    if (Info->FbxSkin) {
        Info->DrawBoneCount = State->Opts->maxDrawBones;
        Info->BlendWeightCount = ComputeBlendWeightCount(State, Info->FbxSkin, Info->VertexCount, State->Opts->maxBlendWeights);
        if (Info->BlendWeightCount > 0) {
            Info->BlendWeights = ComputeBlendWeights(State, Info->FbxSkin, Info->BlendWeightCount, Info->VertexCount);
        }
    }

    if (Info->BlendWeights) {
        Info->TrisToParts = AssignTrisToPartsFromSkin(State, Info);
    } else if (Info->Materials) {
        Info->TrisToParts = AssignTrisToPartsFromMaterials(State, Info);
    } else {
        pre_mesh_part *Part = ArenaAllocT(&State->Arena, pre_mesh_part);
        Part->MaterialID = Info->MatIDs[0];
        Info->MeshParts = Part;
        Info->MeshPartsCount++;
        printf("Packed %d triangles into 1 mesh part by default\n", Info->VertexCount / 3);
    }
}


static inline
u32 HashVertex(f32 *Vertex, u32 VertexSize) {
    u32 Hash = 0;
    for (u32 Index = 0; Index < VertexSize; Index++) {
        u32 Bits = *(u32 *) (Vertex + Index);
        Hash = Hash * 31 + (Bits >> 8); // don't hash the bottom bits, we want loose equality checks.
    }
    if (Hash == 0) Hash = 1;
    return Hash;
}

static inline
bool VertexEquals(float *VertA, float *VertB, int VertexSize) {
    for (u32 Index = 0; Index < VertexSize; Index++) {
        u32 ValA = *(u32 *) (VertA + Index);
        u32 ValB = *(u32 *) (VertB + Index);
        if (ValA >> 8 != ValB >> 8) return false;
    }
    return true;
}

struct mesh_gen_state {
    u32 BlendWeightCount;
    u32 VertexSizeFloats;
    f32 *VertexData;
    u16 *IndexData;
    mesh_node_info *MeshNode;
    u32 VertexIndex;
    u32 IndexIndex;
    u32 TotalDuplicates;
    u32 VertexHashes[65536];
};

static
void InsertVertex(mesh_gen_state *GenState, u32 MeshIndex, skinned_mesh_draw *Draw) {
    f32 TempVertex[MAX_VERTEX_SIZE_FLOATS];

    u32 VertexSizeFloats = GenState->VertexSizeFloats;
    blend_weight *VertWeights = GenState->MeshNode->BlendWeights + MeshIndex * GenState->BlendWeightCount;
    u32 BlendWeightCount = GenState->BlendWeightCount;

    Vec3 Pos = GenState->MeshNode->Positions[MeshIndex];
    Vec3 Nor = {{{0,0,0}}}; // this will be filled in later
    Vec2 UV = GenState->MeshNode->UVs[MeshIndex];
    TempVertex[0] = Pos.x;
    TempVertex[1] = Pos.y;
    TempVertex[2] = Pos.z;
    TempVertex[3] = Nor.x;
    TempVertex[4] = Nor.y;
    TempVertex[5] = Nor.z;
    TempVertex[6] = UV.x;
    TempVertex[7] = UV.y;
    f32 *NextWeight = TempVertex + 8;
    for (u32 WeightIndex = 0; WeightIndex < BlendWeightCount; WeightIndex++) {
        u32 BoneIndex = 0;
        for (; BoneIndex < Draw->NumBoneIDs; BoneIndex++) {
            if (Draw->BoneIDs[BoneIndex] == VertWeights->BoneIndex)
                break;
        }
        Assert(BoneIndex < Draw->NumBoneIDs);
        NextWeight[0] = BoneIndex;
        NextWeight[1] = VertWeights->Weight;
        VertWeights++;
        NextWeight += 2;
    }
    Assert(NextWeight - TempVertex == VertexSizeFloats);

    u32 *VertexHashes = GenState->VertexHashes;
    u32 VertexIndex = GenState->VertexIndex;
    f32 *VertexData = GenState->VertexData;

    u32 Hash = HashVertex(TempVertex, VertexSizeFloats);
    u32 HashIndex;
    for (HashIndex = 0; HashIndex < VertexIndex; HashIndex++) {
        if (Hash == VertexHashes[HashIndex]) {
            if (VertexEquals(VertexData + HashIndex * VertexSizeFloats, TempVertex, VertexSizeFloats)) {
                GenState->TotalDuplicates++;
                break;
            }
        }
    }
    if (HashIndex == VertexIndex) {
        GenState->VertexIndex++;
        memcpy(VertexData + HashIndex * VertexSizeFloats, TempVertex, VertexSizeFloats * sizeof(f32));
    }
    Assert(HashIndex < 65535);
    GenState->IndexData[GenState->IndexIndex++] = (u16) HashIndex;
    VertexHashes[HashIndex] = Hash;
}

struct vec3f {
    f32 x, y, z;
};

static inline
vec3f Add(vec3f a, vec3f b) {
    vec3f result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
    return result;
}

static inline
vec3f Normalized(vec3f V) {
    vec3f Result;
    float Len2 = V.x*V.x + V.y*V.y + V.z*V.z;
    if (Len2 == 0) {
        Result = { 0.0f, 0.0f, 1.0f };
    } else {
        float Len = sqrtf(Len2);
        float ILen = 1.0f / Len;
        Result.x = V.x * ILen;
        Result.y = V.y * ILen;
        Result.z = V.z * ILen;
    }
    return Result;
}

static inline
vec3f Sub(vec3f A, vec3f B) {
    vec3f Result;
    Result.x = A.x - B.x;
    Result.y = A.y - B.y;
    Result.z = A.z - B.z;
    return Result;
}

static inline
vec3f Cross(vec3f A, vec3f B) {
    vec3f Result;
    Result.x = A.y * B.z - B.y * A.z;
    Result.y = A.z * B.x - B.z * A.x;
    Result.z = A.x * B.y - B.x * A.y;
    return Result;
}

static
void FillInNormals(skinning_state *State, skinned_mesh_mesh *OutMesh) {
    u32 Count = OutMesh->VertexCount;
    u32 IndexCount = OutMesh->IndexCount;
    u32 VertexSize = OutMesh->VertexSize;
    f32 *Vertices = OutMesh->VertexData;
    u16 *Indices = OutMesh->IndexData;

    for (u32 TriStart = 0; TriStart < IndexCount; TriStart += 3) {
        u16 Index0 = Indices[TriStart+0];
        u16 Index1 = Indices[TriStart+1];
        u16 Index2 = Indices[TriStart+2];
        f32 *Vertex0 = Vertices + Index0 * VertexSize;
        f32 *Vertex1 = Vertices + Index1 * VertexSize;
        f32 *Vertex2 = Vertices + Index2 * VertexSize;
        vec3f Pos0; memcpy(&Pos0, Vertex0, sizeof(vec3f));
        vec3f Pos1; memcpy(&Pos1, Vertex1, sizeof(vec3f));
        vec3f Pos2; memcpy(&Pos2, Vertex2, sizeof(vec3f));
        vec3f A = Sub(Pos2, Pos1);
        vec3f B = Sub(Pos0, Pos1);
        vec3f Nor = Cross(A, B);
        Nor = Normalized(Nor);
        vec3f *Nor0 = (vec3f *) (Vertex0 + 3);
        vec3f *Nor1 = (vec3f *) (Vertex1 + 3);
        vec3f *Nor2 = (vec3f *) (Vertex2 + 3);
        *Nor0 = Add(*Nor0, Nor);
        *Nor1 = Add(*Nor1, Nor);
        *Nor2 = Add(*Nor2, Nor);
    }

    for (u32 Index = 0; Index < Count; Index++) {
        f32 *Vertex = Vertices + Index * VertexSize;
        vec3f *Nor = (vec3f *) (Vertex + 3);
        *Nor = Normalized(*Nor);
    }
}

static
void GenerateMeshData(skinning_state *State, mesh_type *Type, skinned_mesh_mesh *OutMesh, u16 MeshID) {
    u32 MaxVertices = 0;
    {
        mesh_node_info *MeshNode = Type->Nodes;
        while (MeshNode) {
            MaxVertices += MeshNode->VertexCount;
            MeshNode = MeshNode->Next;
        }
    }

    mesh_gen_state GenState = {};

    GenState.BlendWeightCount = Type->BlendWeightCount;
    // position, normal, uv, bone, weight, bone, weight, ...
    GenState.VertexSizeFloats = 3 + 3 + 2 + (2 * GenState.BlendWeightCount);

    GenState.VertexData = ArenaAllocTN(&State->Arena, f32, MaxVertices * GenState.VertexSizeFloats);
    GenState.IndexData = ArenaAllocTN(&State->Arena, u16, MaxVertices);

    GenState.VertexIndex = 0;
    GenState.IndexIndex = 0;

    GenState.MeshNode = Type->Nodes;

    while (GenState.MeshNode) {
        u32 MeshVertexCount = GenState.MeshNode->VertexCount;
        Assert(MeshVertexCount % 3 == 0);
        u16 *TrisToParts = GenState.MeshNode->TrisToParts;

        u32 PartID = 0;
        pre_mesh_part *MeshPart = GenState.MeshNode->MeshParts;
        Assert(MeshPart);
        while (MeshPart) {
            skinned_mesh_draw *Draw = &State->Mesh->Draws[State->DrawIndex++];
            Draw->MaterialID = MeshPart->MaterialID;
            Draw->MeshID = MeshID;
            Draw->MeshOffset = GenState.IndexIndex / 3;
            Draw->ParentBoneID = GenState.MeshNode->ParentID;
            u32 BoneIndex;
            for (BoneIndex = 0; BoneIndex < MAX_DRAW_BONES; BoneIndex++) {
                s32 BoneID = MeshPart->BoneIDs[BoneIndex];
                if (BoneID < 0) break;
                Draw->BoneIDs[BoneIndex] = (u16) BoneID;
            }
            Draw->NumBoneIDs = BoneIndex;

            for (u32 TriIndex = 0, MeshIndex = 0;
                MeshIndex < MeshVertexCount; TriIndex++,
                MeshIndex += 3)
            {
                if (TrisToParts && TrisToParts[TriIndex] != PartID) continue;

                InsertVertex(&GenState, MeshIndex+0, Draw);
                InsertVertex(&GenState, MeshIndex+1, Draw);
                InsertVertex(&GenState, MeshIndex+2, Draw);
            }

            Assert(GenState.IndexIndex % 3 == 0);
            Draw->MeshLength = GenState.IndexIndex / 3 - Draw->MeshOffset;
            PartID++;
            MeshPart = MeshPart->Next;
        }

        GenState.MeshNode = GenState.MeshNode->Next;
    }

    Assert(GenState.VertexIndex < 65536);
    Assert(GenState.IndexIndex < 65536);
    OutMesh->VertexCount = (u16) GenState.VertexIndex;
    OutMesh->IndexCount = (u16) GenState.IndexIndex;
    OutMesh->VertexSize = (u16) GenState.VertexSizeFloats;
    OutMesh->BoneCount = (u16) GenState.BlendWeightCount;
    OutMesh->VertexData = GenState.VertexData;
    OutMesh->IndexData = GenState.IndexData;

    FillInNormals(State, OutMesh);

    printf("Used %d Vertices and %d Indices\n", GenState.VertexIndex, GenState.IndexIndex);
    printf("Found %d Duplicate Vertices\n", GenState.TotalDuplicates);

    // // reify the MeshNode parts
    // Matrix geomTf = MeshNode->getGeometricMatrix();
    // Matrix meshTf = MeshNode->getGlobalTransform();
    // Matrix nodeTf = mul(&meshTf, &geomTf);
    // outMesh->parts.reserve(outMesh->parts.size() + data.parts.size());
    // int partID = 0;
    // for (PreMeshPart &part : data.parts) {
    //     // make a mesh part
    //     outMesh->parts.emplace_back();
    //     MeshPart &mp = outMesh->parts.back();

    //     // give it a name
    //     findName(MeshNode, "Model", mp.id);
    //     std::stringstream builder;
    //     builder << mp.id << '_' << partID;
    //     mp.id = builder.str();

    //     // build the vertices and indices
    //     mp.primitive = PRIMITIVETYPE_TRIANGLES;
    //     buildMesh(&data, outMesh, &mp.indices, partID, &part);

    //     // attach rendering info to the node
    //     node->parts.emplace_back();
    //     NodePart *np = &node->parts.back();
    //     np->meshPartID = mp.id;
    //     np->materialID = model->materials[baseIdx + part.material].id;
    //     addBones(&data, &part, np, &nodeTf);

    //     partID++;
    // }
}


static
u16 AppendLimbNode(skinning_state *State, const Object *Node, u16 ParentID) {
    u16 NodeID = State->NextNodeID++;

    limb_node_info *Info = ArenaAllocT(&State->Arena, limb_node_info);
    Info->ID = NodeID;
    Info->ParentID = ParentID;
    Info->Source = Node;
    Info->Next = State->Limbs;
    State->Limbs = Info;

    return NodeID;
}

static
u16 ConvertNode(skinning_state *State, const Object *Node, u16 ParentID) {
    u16 NodeID = ParentID;

    switch (Node->getType()) {
    case Object::Type::MESH: {
        mesh_node_info *Info = ArenaAllocT(&State->Arena, mesh_node_info);
        Info->ParentID = ParentID;
        Info->Source = dynamic_cast<const Mesh *>(Node);
        Info->Next = State->Meshes;
        State->Meshes = Info;
    } break;

    case Object::Type::LIMB_NODE: {
        NodeID = AppendLimbNode(State, Node, ParentID);
    } break;

    default: break;
    }

    return NodeID;
}

static
void ConvertFBXToMeshRecursive(skinning_state *State, const Object *Node, u16 ParentID) {
    const Object *Child;
    for (int i = 0; (Child = Node->resolveObjectLink(i)); i++) {
        if (Child->isNode()) {
            u16 NodeID = ConvertNode(State, Child, ParentID);
            ConvertFBXToMeshRecursive(State, Child, NodeID);
        }
    }
}

static
void SortMeshNode(skinning_state *State, mesh_node_info *Node) {
    mesh_type **PType = &State->MeshTypes;
    mesh_type *Type;
    while (1) {
        Type = *PType;
        if (!Type) {
            Type = ArenaAllocT(&State->Arena, mesh_type);
            Type->BlendWeightCount = Node->BlendWeightCount;
            *PType = Type;
            break;
        }

        if (Type->BlendWeightCount == Node->BlendWeightCount) {
            break;
        }

        PType = &Type->Next;
    }
    Node->Next = Type->Nodes;
    Type->Nodes = Node;
}

static
void CopyTransform(transform *Out, Vec3 &Translation, Quat &Rotation, Vec3 &Scale) {
    Out->Translation.x = (f32) Translation.x;
    Out->Translation.y = (f32) Translation.y;
    Out->Translation.z = (f32) Translation.z;
    Out->Rotation.x = (f32) Rotation.x;
    Out->Rotation.y = (f32) Rotation.y;
    Out->Rotation.z = (f32) Rotation.z;
    Out->Rotation.w = (f32) Rotation.w;
    Out->Scale.x = (f32) Scale.x;
    Out->Scale.y = (f32) Scale.y;
    Out->Scale.z = (f32) Scale.z;
}

skinned_mesh_with_pose *ConvertFBXToSkinnedMesh(Options *Opts, const IScene *Scene) {
    skinning_state State = {};
    ArenaInit(&State.Arena, calloc(TMP_MEM_SIZE, 1), TMP_MEM_SIZE);
    skinned_mesh_with_pose *Result = ArenaAllocT(&State.Arena, skinned_mesh_with_pose);
    State.Mesh = &Result->Mesh;
    State.BoneNames = &Result->BoneNames;
    State.Opts = Opts;
    State.NextNodeID = 0;

    const Object *Root = Scene->getRoot();
    AppendLimbNode(&State, Root, 0);

    // iterate the FBX tree and find all mesh and limb nodes
    ConvertFBXToMeshRecursive(&State, Root, 0);

    // copy bone names into a lookup table for later
    // also prepare the bind pose
    State.BoneCount = ReverseLinkedList((void **) &State.Limbs);
    limb_node_info *Limb = State.Limbs;
    u32 LimbID = 0;
    Result->BindPose.BoneCount = State.BoneCount;
    Result->BindPose.BoneParentIDs = ArenaAllocTN(&State.Arena, u16, State.BoneCount);
    Result->BindPose.SetupPose = ArenaAllocTN(&State.Arena, transform, State.BoneCount);
    Result->BoneNames.BoneCount = State.BoneCount;
    Result->BoneNames.Names = ArenaAllocTN(&State.Arena, node_name, State.BoneCount);
    while (Limb) {
        const Object *Node = Limb->Source;
        strncpy(Result->BoneNames.Names[LimbID].Name, Node->name, MAX_NODE_NAME_LENGTH);
        Result->BindPose.BoneParentIDs[LimbID] = Limb->ParentID;
        transform *BindPose = Result->BindPose.SetupPose + LimbID;
        Matrix LocalTransform = Node->evalLocal(Node->getLocalTranslation(), Node->getLocalRotation());
        ExtractTransform(&LocalTransform, (f32 *) &BindPose->Translation, (f32 *) &BindPose->Rotation, (f32 *) &BindPose->Scale);
        printf("%s: [(%f %f %f) (%f %f %f %f) (%f %f %f)]\n", Node->name,
            BindPose->Translation.x, BindPose->Translation.y, BindPose->Translation.z,
            BindPose->Rotation.x, BindPose->Rotation.y, BindPose->Rotation.z, BindPose->Rotation.w,
            BindPose->Scale.x, BindPose->Scale.y, BindPose->Scale.z);
        Limb = Limb->Next;
        LimbID++;
    }
    Assert(LimbID == State.BoneCount);

    // process the meshes and split them up by vertex format
    u32 MeshPartsCount = 0;
    ReverseLinkedList((void **) &State.Meshes);
    mesh_node_info *Mesh = State.Meshes;
    while (Mesh) {
        mesh_node_info *Next = Mesh->Next;
        ConvertMeshNode(&State, Mesh);
        SortMeshNode(&State, Mesh);
        MeshPartsCount += Mesh->MeshPartsCount;
        Mesh = Next;
    }
    State.Meshes = 0;

    State.Mesh->DrawCount = MeshPartsCount;
    State.Mesh->Draws = ArenaAllocTN(&State.Arena, skinned_mesh_draw, MeshPartsCount);
    State.DrawIndex = 0;

    // for each mesh format, generate one output mesh
    u32 MeshTypeCount = CountLinkedList(State.MeshTypes);
    State.Mesh->MeshCount = MeshTypeCount;
    State.Mesh->Meshes = ArenaAllocTN(&State.Arena, skinned_mesh_mesh, MeshTypeCount);
    u32 MeshTypeID = 0;
    mesh_type *MeshType = State.MeshTypes;
    while (MeshType) {
        GenerateMeshData(&State, MeshType, State.Mesh->Meshes + MeshTypeID, (u16) MeshTypeID);
        MeshType = MeshType->Next;
        MeshTypeID++;
    }

    Assert(State.DrawIndex == MeshPartsCount);

    u32 TexCount = CountLinkedList(State.Textures);
    State.Mesh->TextureCount = TexCount;
    State.Mesh->Textures = ArenaAllocTN(&State.Arena, texture, TexCount);

    texture_info *Tex = State.Textures;
    u32 TexIndex = 0;
    while (Tex) {
        State.Mesh->Textures[TexIndex].TexturePath = Tex->File;
        TexIndex++;
        Tex = Tex->Next;
    }

    u32 MatCount = CountLinkedList(State.Materials);
    State.Mesh->MaterialCount = MatCount;
    State.Mesh->Materials = ArenaAllocTN(&State.Arena, material, MatCount);

    material_info *Mat = State.Materials;
    u32 MatIndex = 0;
    while (Mat) {
        State.Mesh->Materials[MatIndex] = Mat->Mat;
        MatIndex++;
        Mat = Mat->Next;
    }

    printf("Mesh conversion used %u / %u bytes.\n", State.Arena.Pos, State.Arena.Capacity);

    return Result;
}

bool WriteMesh(skinned_mesh_with_pose *MeshPose, const char *Filename) {
    skinned_mesh *Mesh = &MeshPose->Mesh;
    skeleton_pose *Pose = &MeshPose->BindPose;

    FILE *File = fopen(Filename, "wb");
    if (!File) {
        printf("Couldn't fopen %s\n", Filename);
        return false;
    }

    struct {
        u32 VertexStart;
        u32 IndexStart;
        u32 PoseStart;
    } Pointers = {};


    fwrite(Mesh, 4 * sizeof(u16), 1, File);
    fwrite(&Pointers, sizeof(Pointers), 1, File); // these will be filled in with actual data later
    fwrite(Mesh->Draws, Mesh->DrawCount * sizeof(skinned_mesh_draw), 1, File);

    struct {
        u32 BaseVertex;
        u32 BaseIndex;
    } Positions = {};
    for (u32 MeshIndex = 0; MeshIndex < Mesh->MeshCount; MeshIndex++) {
        skinned_mesh_mesh *MeshData = Mesh->Meshes + MeshIndex;
        fwrite(MeshData, 4 * sizeof(u16), 1, File);
        fwrite(&Positions, sizeof(Positions), 1, File);
        Positions.BaseVertex += MeshData->VertexCount * MeshData->VertexSize;
        Positions.BaseIndex += MeshData->IndexCount;
    }

    fwrite(Mesh->Materials, Mesh->MaterialCount * sizeof(material), 1, File);

    for (u32 TexIndex = 0; TexIndex < Mesh->TextureCount; TexIndex++) {
        texture *Tex = Mesh->Textures + TexIndex;
        int Len = strlen(Tex->TexturePath) + 1;
        Assert(Len < 256);
        u8 ShortLen = (u8) Len;
        fwrite(&ShortLen, 1, 1, File);
        fwrite(Tex->TexturePath, ShortLen, 1, File);
    }

    u32 FilePos = ftell(File);
    u32 Target = AlignRoundUp(FilePos, 4);
    if (FilePos < Target) {
        u32 Pad = 0;
        fwrite(&Pad, Target - FilePos, 1, File);
        FilePos = Target;
    }

    Pointers.VertexStart = FilePos;
    for (u32 MeshIndex = 0; MeshIndex < Mesh->MeshCount; MeshIndex++) {
        skinned_mesh_mesh *MeshData = Mesh->Meshes + MeshIndex;
        u32 ByteSize = MeshData->VertexCount * MeshData->VertexSize * sizeof(f32);
        fwrite(MeshData->VertexData, ByteSize, 1, File);
        FilePos += ByteSize;
    }
    Pointers.IndexStart = FilePos;
    for (u32 MeshIndex = 0; MeshIndex < Mesh->MeshCount; MeshIndex++) {
        skinned_mesh_mesh *MeshData = Mesh->Meshes + MeshIndex;
        u32 ByteSize = MeshData->IndexCount * sizeof(u16);
        fwrite(MeshData->IndexData, ByteSize, 1, File);
        FilePos += ByteSize;
    }

    Target = AlignRoundUp(FilePos, 4);
    if (FilePos < Target) {
        u32 Pad = 0;
        fwrite(&Pad, Target - FilePos, 1, File);
        FilePos = Target;
    }

    Pointers.PoseStart = FilePos;
    fwrite(&Pose->BoneCount, 1, sizeof(u16), File);
    fwrite(Pose->BoneParentIDs, Pose->BoneCount, sizeof(u16), File);
    if (Pose->BoneCount % 2 == 0) {
        // pad out to 4 byte aligned
        u16 Pad = 0;
        fwrite(&Pad, sizeof(u16), 1, File);
    }
    fwrite(Pose->SetupPose, Pose->BoneCount, sizeof(transform), File);

    fseek(File, 4 * sizeof(u16), SEEK_SET);
    fwrite(&Pointers, sizeof(Pointers), 1, File);

    fclose(File);

    return true;
}

bool WriteBoneAssignment(bone_assignment *Assignment, const char *Filename) {
    FILE *File = fopen(Filename, "wb");
    if (!File) {
        printf("Couldn't write bone assignment file %s\n", Filename);
        return false;
    }

    fwrite(&Assignment->BoneCount, 1, sizeof(u32), File);
    fwrite(Assignment->Names, Assignment->BoneCount, sizeof(node_name), File);

    fclose(File);

    return true;
}

bone_assignment ReadBoneAssignment(const char *Filename, memory_arena *Arena) {
    bone_assignment Result = {};

    FILE *File = fopen(Filename, "rb");
    Assert(File);
    if (File) {
        fread(&Result.BoneCount, 1, sizeof(u32), File);
        Result.Names = ArenaAllocTN(Arena, node_name, Result.BoneCount);
        fread(Result.Names, Result.BoneCount, sizeof(node_name), File);
        fclose(File);
    }

    return Result;
}
