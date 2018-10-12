#include "animation_types.h"

static
void UploadMeshesToOGL(skinned_mesh *Mesh) {
    for (u32 MeshIndex = 0; MeshIndex < Mesh->MeshCount; MeshIndex++) {
        skinned_mesh_mesh *MeshData = Mesh->Meshes + MeshIndex;
        u32 VertexSizeBytes = MeshData->VertexCount * MeshData->VertexSize * sizeof(f32);
        glGenVertexArrays(1, &MeshData->VaoID);
        glBindVertexArray(MeshData->VaoID);
        glGenBuffers(2, MeshData->BufferIDs);
        glBindBuffer(GL_ARRAY_BUFFER, MeshData->BufferIDs[0]);
        glBufferData(GL_ARRAY_BUFFER, VertexSizeBytes, MeshData->VertexData, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, MeshData->BufferIDs[1]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, MeshData->IndexCount * sizeof(u16), MeshData->IndexData, GL_STATIC_DRAW);

        // vertex format:
        // position 3
        // normal 3
        // uv 2
        // weights 2 * n
        u32 Stride = MeshData->VertexSize * sizeof(f32);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, Stride, (void *) (0 * sizeof(f32)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, Stride, (void *) (3 * sizeof(f32)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, Stride, (void *) (6 * sizeof(f32)));
        glEnableVertexAttribArray(2);
        u32 FloatPos = 8;
        u32 AttribPos = 3;
        for (u32 BoneIndex = 0; BoneIndex < MeshData->BoneCount; BoneIndex++) {
            glVertexAttribPointer(AttribPos, 2, GL_FLOAT, GL_FALSE, Stride, (void *) (FloatPos * sizeof(f32)));
            glEnableVertexAttribArray(AttribPos);
            AttribPos++;
            FloatPos += 2;
        }
    }
}


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