#include "animation_types.h"

static
skinned_mesh *LoadMeshData(memory_arena *Arena, void *FileData) {
    skinned_mesh *Mesh = ArenaAllocT(Arena, skinned_mesh);
    char *FilePos = (char *)FileData;

    memcpy(Mesh, FilePos, 4 * sizeof(u16));
    FilePos += 4 * sizeof(u16);

    struct {
        u32 VertexStart;
        u32 IndexStart;
    } Pointers;
    memcpy(&Pointers, FilePos, sizeof(Pointers));
    FilePos += sizeof(Pointers);

    f32 *VertexData = (f32 *)((char *)FileData + Pointers.VertexStart);
    u16 *IndexData = (u16 *)((char *)FileData + Pointers.IndexStart);

    Mesh->Draws = (skinned_mesh_draw *) FilePos;
    FilePos += Mesh->DrawCount * sizeof(skinned_mesh_draw);

    Mesh->Meshes = ArenaAllocTN(Arena, skinned_mesh_mesh, Mesh->MeshCount);
    for (u32 MeshIndex = 0; MeshIndex < Mesh->MeshCount; MeshIndex++) {
        skinned_mesh_mesh *MeshData = Mesh->Meshes + MeshIndex;
        memcpy(MeshData, FilePos, 4 * sizeof(u16));
        FilePos += 4 * sizeof(u16);

        struct {
            u32 VertexIndex;
            u32 IndexIndex;
        } Positions;
        memcpy(&Positions, FilePos, sizeof(Positions));
        FilePos += sizeof(Positions);

        MeshData->VertexData = VertexData + Positions.VertexIndex;
        MeshData->IndexData = IndexData + Positions.IndexIndex;
    }

    Mesh->Materials = (material *)FilePos;
    FilePos += Mesh->MaterialCount * sizeof(material);

    Mesh->Textures = ArenaAllocTN(Arena, texture, Mesh->TextureCount); 
    for (u32 TextureIndex = 0; TextureIndex < Mesh->TextureCount; TextureIndex++) {
        texture *Tex = Mesh->Textures + TextureIndex;
        u8 skip = (u8) *FilePos;
        FilePos++;
        Tex->TexturePath = FilePos;
        FilePos += skip;
    }

    return Mesh;
}