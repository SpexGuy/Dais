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
        CheckGLError();
    }
}

const char *DefaultVertexShader = GLSL(
    layout(location=0) in vec3 Position;
    layout(location=1) in vec3 Normal;
    layout(location=2) in vec2 UV;

    uniform mat4 Projection;

    out vec2 InterpUV;

    void main() {
        gl_Position = Projection * vec4(Position, 1.0);
        InterpUV = UV;
    }
);

const char *DefaultFragmentShader = GLSL(
    in vec2 InterpUV;

    out vec4 Color;

    void main() {
        Color = vec4(InterpUV, 0.0, 1.0);
    }
);

struct shader_state {
    u32 ProgramID;
    u32 Projection;
};

static
shader_state *InitShaders(memory_arena *Arena) {
    shader_state *State = ArenaAllocT(Arena, shader_state);
    State->ProgramID = CompileShader(DefaultVertexShader, DefaultFragmentShader);
    State->Projection = glGetUniformLocation(State->ProgramID, "Projection");
    CheckGLError();
    return State;
}

static
void RenderSkinnedMesh(shader_state *Shaders, skinned_mesh *Mesh, glm::mat4 &Projection) {
    glUseProgram(Shaders->ProgramID);
    glUniformMatrix4fv(Shaders->Projection, 1, GL_FALSE, &Projection[0][0]);
    for (u32 DrawIndex = 0; DrawIndex < Mesh->DrawCount; DrawIndex++) {
        skinned_mesh_draw *Draw = Mesh->Draws + DrawIndex;
        Assert(Draw->MaterialID > 0);
        Assert(Draw->MaterialID <= Mesh->MaterialCount);
        Assert(Draw->MeshID < Mesh->MeshCount);
        material *Material = Mesh->Materials + Draw->MaterialID-1;
        skinned_mesh_mesh *MeshData = Mesh->Meshes + Draw->MeshID;
        glBindVertexArray(MeshData->VaoID);
        glDrawElements(GL_TRIANGLES, Draw->MeshLength, GL_UNSIGNED_SHORT, (void *)(Draw->MeshOffset * sizeof(u16)));

        // if (Material->DiffuseTexID != 0) {
        //     texture *DiffuseTex = Mesh->Textures + Material->DiffuseTexID-1;
        //     glActiveTexture(GL_TEXTURE_0);
        //     glBindTexture(GL_TEXTURE_2D, DiffuseTex->GLTexID);
        // }

        // if (Material->NormalTexID != 0) {
        //     texture *NormalTex = Mesh->Textures + Material->NormalTexID-1;
        //     glActiveTexture(GL_TEXTURE_0);
        //     glBindTexture(GL_TEXTURE_2D, NormalTex->GLTexID);
        // }

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