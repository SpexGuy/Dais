struct normal_mesh {
    u32 VaoID;
    u32 Count;
};

static
void LoadTexture(GLuint Texname, const char *Filename) {
    glBindTexture(GL_TEXTURE_2D, Texname);

    int Width, Height, Bpp;
    unsigned char *Pixels = stbi_load(Filename, &Width, &Height, &Bpp, STBI_default);
    if (!Pixels) {
        printf("Failed to load image %s (%s)\n", Filename, stbi_failure_reason());
        return;
    }
    printf("Loaded %s, %dx%d, comp=%d\n", Filename, Width, Height, Bpp);

    GLenum Format;
    switch(Bpp) {
    case STBI_rgb:
        Format = GL_RGB;
        break;
    case STBI_rgb_alpha:
        Format = GL_RGBA;
        break;
    default:
        printf("Unsupported format: %d\n", Bpp);
        return;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, Width, Height, 0, Format, GL_UNSIGNED_BYTE, Pixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(Pixels);
}

static
normal_mesh *GenerateNormalMeshes(memory_arena *Perm, memory_arena *Temp, skinned_mesh *Mesh) {
    normal_mesh *Nors = ArenaAllocTN(Perm, normal_mesh, Mesh->MeshCount);
    for (u32 MeshIndex = 0; MeshIndex < Mesh->MeshCount; MeshIndex++) {
        normal_mesh *Nor = Nors + MeshIndex;
        skinned_mesh_mesh *MeshData = Mesh->Meshes + MeshIndex;
        u32 TempRestore = Temp->Pos;
        Nor->Count = MeshData->VertexCount;
        vec3 *NormalBuffer = ArenaAllocTN(Temp, vec3, Nor->Count * 2);
        u32 VertexSize = MeshData->VertexSize;
        vec3 *NormalPos = NormalBuffer;
        for (u32 VertexIndex = 0; VertexIndex < MeshData->VertexCount; VertexIndex++) {
            f32 *Vertex = MeshData->VertexData + (VertexIndex * VertexSize);
            vec3 Pos(Vertex[0], Vertex[1], Vertex[2]);
            vec3 Nor(Vertex[3], Vertex[4], Vertex[5]);
            NormalPos[0] = Pos;
            NormalPos[1] = Pos + Nor * 10.0f;
            NormalPos += 2;
        }

        u32 BufferID;
        glGenVertexArrays(1, &Nor->VaoID);
        glBindVertexArray(Nor->VaoID);
        glGenBuffers(1, &BufferID);
        glBindBuffer(GL_ARRAY_BUFFER, BufferID);
        glBufferData(GL_ARRAY_BUFFER, Nor->Count * 2 * sizeof(vec3), NormalBuffer, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), 0);
        glEnableVertexAttribArray(0);

        CheckGLError();

        ArenaRestore(Temp, TempRestore);
    }
    return Nors;
}

static
void UploadMeshesToOGL(skinned_mesh *Mesh) {
    for (u32 MeshIndex = 0; MeshIndex < Mesh->MeshCount; MeshIndex++) {
        printf("Starting mesh %u of %hu\n", MeshIndex+1, Mesh->MeshCount);
        skinned_mesh_mesh *MeshData = Mesh->Meshes + MeshIndex;
        u32 VertexSizeBytes = MeshData->VertexCount * MeshData->VertexSize * sizeof(f32);
        printf("Vertex size: %hu floats (%d bytes)\n", MeshData->VertexSize, MeshData->VertexSize * 4);
        glGenVertexArrays(1, &MeshData->VaoID);
        glBindVertexArray(MeshData->VaoID);
        glGenBuffers(2, MeshData->BufferIDs);
        glBindBuffer(GL_ARRAY_BUFFER, MeshData->BufferIDs[0]);
        printf("Uploading %hu vertices (%u bytes)\n", MeshData->VertexCount, VertexSizeBytes);
        glBufferData(GL_ARRAY_BUFFER, VertexSizeBytes, MeshData->VertexData, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, MeshData->BufferIDs[1]);
        printf("Uploading %hu indices (%lu bytes)\n", MeshData->IndexCount, MeshData->IndexCount * sizeof(u16));
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, MeshData->IndexCount * sizeof(u16), MeshData->IndexData, GL_STATIC_DRAW);

        // vertex format:
        // position 3
        // normal 3
        // uv 2
        // weights 2 * n
        CheckGLError();
        u32 Stride = MeshData->VertexSize * sizeof(f32);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, Stride, (void *) (0 * sizeof(f32)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, Stride, (void *) (3 * sizeof(f32)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, Stride, (void *) (6 * sizeof(f32)));
        glEnableVertexAttribArray(2);
        u32 FloatPos = 8;
        u32 AttribPos = 3;
        printf("Configuring %d bone weights\n", MeshData->BoneCount);
        for (u32 BoneIndex = 0; BoneIndex < MeshData->BoneCount; BoneIndex++) {
            glVertexAttribPointer(AttribPos, 2, GL_FLOAT, GL_FALSE, Stride, (void *) (FloatPos * sizeof(f32)));
            glEnableVertexAttribArray(AttribPos);
            CheckGLError_(__FILE__,FloatPos);
            AttribPos++;
            FloatPos += 2;
        }

        CheckGLError();
    }

    for (u32 TexIndex = 0; TexIndex < Mesh->TextureCount; TexIndex++) {
        texture *Tex = Mesh->Textures + TexIndex;
        const int BufSize = 256;
        char Buffer[BufSize];
        strcpy(Buffer, "../Avatar/");
        strncat(Buffer, Tex->TexturePath, BufSize-1);
        glGenTextures(1, &Tex->GLTexID);
        LoadTexture(Tex->GLTexID, Buffer);
        CheckGLError();
    }
}

const char *DefaultVertexShader = GLSL(
    layout(location=0) in vec3 Position;
    layout(location=1) in vec3 Normal;
    layout(location=2) in vec2 UV;

    uniform mat4 Projection;

    out vec3 InterpNormal;

    void main() {
        gl_Position = Projection * vec4(Position, 1.0);
        InterpNormal = Normal;
    }
);

const char *DefaultFragmentShader = GLSL(
    in vec3 InterpNormal;

    out vec4 Color;

    void main() {
        Color = vec4(abs(InterpNormal), 1.0);
    }
);

const char *TexVertexShader = GLSL(
    layout(location=0) in vec3 Position;
    layout(location=1) in vec3 Normal;
    layout(location=2) in vec2 UV;

    uniform mat4 Projection;

    out vec2 InterpUV;

    void main() {
        gl_Position = Projection * vec4(Position, 1.0);
        InterpUV = vec2(UV.x, 1.0 - UV.y);
    }
);

const char *TexFragmentShader = GLSL(
    in vec2 InterpUV;

    uniform sampler2D DiffuseTexture;

    out vec4 Color;

    void main() {
        Color = texture(DiffuseTexture, InterpUV);
    }
);

const char *SkinHeader = GLSL_VERSION "#define NUM_WEIGHTS %d\n#define NUM_BONES %d\n";
const char *SkinVertexShader = MULTILINE_STR(
    layout(location=0) in vec3 Position;
    layout(location=1) in vec3 Normal;
    layout(location=2) in vec2 UV;
    layout(location=3) in vec2 Weights[NUM_WEIGHTS];

    uniform mat4 Projection;
    uniform mat4x3 Bones[NUM_BONES];

    out vec2 InterpUV;

    void main() {
        vec3 SkinnedPosition = vec3(0.0);
        for (int c = 0; c < NUM_WEIGHTS; c++) {
            int Index = int(Weights[c].x);
            float Weight = Weights[c].y;
            SkinnedPosition += (Bones[Index] * vec4(Position, 1.0)) * Weight;
        }
        gl_Position = Projection * vec4(SkinnedPosition, 1.0);
        InterpUV = vec2(UV.x, 1.0 - UV.y);
    }
);

const char *SkinFragmentShader = TexFragmentShader;

const char *NorVertexShader = GLSL(
    layout(location=0) in vec3 Position;

    uniform mat4 Projection;

    void main() {
        gl_Position = Projection * vec4(Position, 1.0);
    }
);

const char *NorFragmentShader = GLSL(
    out vec4 Color;

    void main() {
        Color = vec4(0.0, 1.0, 1.0, 1.0);
    }
);

#define MAX_BONES MAX_BONE_IDS_PER_DRAW
#define MAX_SKIN_SHADERS 20

struct skin_shader {
    u16 NumWeights;
    u16 NumBones;
    u32 ProgramID;
    u32 Projection;
    u32 DiffuseTexture;
    u32 Bones;
};

struct shader_state {
    u32 ProgramID;
    u32 Projection;
    u32 NorProgramID;
    u32 NorProjection;
    u32 TexProgramID;
    u32 TexProjection;
    u32 TexDiffuseTexture;
    u32 TmpVertexBuffer;
    u32 TmpVao;
    skin_shader *SkinShaders[MAX_SKIN_SHADERS];
};

static
skin_shader *FindOrCreateSkinShader(shader_state *State, u16 NumWeights, u16 NumBones) {
    u32 Index = 0;
    for (; Index < MAX_SKIN_SHADERS; Index++) {
        skin_shader *Shader = State->SkinShaders[Index];
        if (!Shader) break;
        if (Shader->NumWeights == NumWeights && Shader->NumBones == NumBones) {
            return Shader;
        }
    }
    Assert(Index < MAX_SKIN_SHADERS);

    printf("Creating Skinning Shader with %d Weights and %d Bones\n", NumWeights, NumBones);
    skin_shader *Shader = ArenaAllocT(PermArena, skin_shader);
    Shader->NumWeights = NumWeights;
    Shader->NumBones = NumBones;
    State->SkinShaders[Index] = Shader;

    char *VertHeader = TPrintf(SkinHeader, (int)NumWeights, (int)NumBones);
    char *Vert = TCat(VertHeader, SkinVertexShader);
    const char *Frag = SkinFragmentShader;
    Shader->ProgramID = CompileShader(Vert, Frag);
    Shader->Projection = glGetUniformLocation(Shader->ProgramID, "Projection");
    Shader->DiffuseTexture = glGetUniformLocation(Shader->ProgramID, "DiffuseTexture");
    Shader->Bones = glGetUniformLocation(Shader->ProgramID, "Bones");
    Assert(Shader->Bones >= 0);
    CheckGLError();
    return Shader;
}

static
shader_state *InitShaders(memory_arena *Arena) {
    shader_state *State = ArenaAllocT(Arena, shader_state);
    State->ProgramID = CompileShader(DefaultVertexShader, DefaultFragmentShader);
    State->Projection = glGetUniformLocation(State->ProgramID, "Projection");
    State->NorProgramID = CompileShader(NorVertexShader, NorFragmentShader);
    State->NorProjection = glGetUniformLocation(State->NorProgramID, "Projection");
    State->TexProgramID = CompileShader(TexVertexShader, TexFragmentShader);
    State->TexProjection = glGetUniformLocation(State->TexProgramID, "Projection");
    State->TexDiffuseTexture = glGetUniformLocation(State->TexProgramID, "DiffuseTexture");
    glGenBuffers(1, &State->TmpVertexBuffer);
    glGenVertexArrays(1, &State->TmpVao);
    CheckGLError();
    return State;
}

static
void RenderNormals(shader_state *Shaders, normal_mesh *Meshes, u32 NorCount, mat4 &Projection) {
    glUseProgram(Shaders->NorProgramID);
    glUniformMatrix4fv(Shaders->NorProjection, 1, GL_FALSE, &Projection[0][0]);
    for (u32 NorIndex = 0; NorIndex < NorCount; NorIndex++) {
        normal_mesh *Draw = Meshes + NorIndex;
        glBindVertexArray(Draw->VaoID);
        glDrawArrays(GL_LINES, 0, Draw->Count * 2);
    }
    CheckGLError();
}

static
void RenderBones(shader_state *Shaders, skeleton *Skel, mat4 &Projection) {
    glUseProgram(Shaders->NorProgramID);
    glUniformMatrix4fv(Shaders->NorProjection, 1, GL_FALSE, &Projection[0][0]);

    glBindVertexArray(Shaders->TmpVao);
    glBindBuffer(GL_ARRAY_BUFFER, Shaders->TmpVertexBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), 0);
    CheckGLError();
    vec3 Points[2];
    for (u32 c = 1; c < Skel->Pose.BoneCount; c++) {
        u32 Parent = Skel->Pose.BoneParentIDs[c];
        mat4x3 &ParentTransform = Skel->WorldMatrices[Parent];
        mat4x3 &CurrentTransform = Skel->WorldMatrices[c];
        mat4x3 &ParentBase = Skel->WorldSetupMatrices[Parent];
        mat4x3 &CurrentBase = Skel->WorldSetupMatrices[c];
        Points[0] = ParentTransform * vec4(ParentBase[3], 1.0);
        Points[1] = CurrentTransform * vec4(CurrentBase[3], 1.0);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Points), &Points, GL_STREAM_DRAW);
        CheckGLError();
        glDrawArrays(GL_LINES, 0, 2);
        CheckGLError();
    }
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CheckGLError();
}

static
void RenderMesh(shader_state *Shaders, skinned_mesh *Mesh, mat4 &Projection) {
    for (u32 DrawIndex = 0; DrawIndex < Mesh->DrawCount; DrawIndex++) {
        skinned_mesh_draw *Draw = Mesh->Draws + DrawIndex;
        Assert(Draw->MaterialID > 0);
        Assert(Draw->MaterialID <= Mesh->MaterialCount);
        Assert(Draw->MeshID < Mesh->MeshCount);
        material *Material = Mesh->Materials + Draw->MaterialID-1;
        skinned_mesh_mesh *MeshData = Mesh->Meshes + Draw->MeshID;
        glBindVertexArray(MeshData->VaoID);
        if (Material->DiffuseTexID == 0) {
            glUseProgram(Shaders->ProgramID);
            glUniformMatrix4fv(Shaders->Projection, 1, GL_FALSE, &Projection[0][0]);
        } else {
            glUseProgram(Shaders->TexProgramID);
            glUniformMatrix4fv(Shaders->TexProjection, 1, GL_FALSE, &Projection[0][0]);
            glUniform1i(Shaders->TexDiffuseTexture, 0);
            u32 GLTexID = Mesh->Textures[Material->DiffuseTexID-1].GLTexID;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, GLTexID);
        }

        glDrawElements(GL_TRIANGLES, Draw->MeshLength * 3, GL_UNSIGNED_SHORT, (void *)(Draw->MeshOffset * sizeof(u16) * 3));

        // if (Material->NormalTexID != 0) {
        //     texture *NormalTex = Mesh->Textures + Material->NormalTexID-1;
        //     glActiveTexture(GL_TEXTURE_0);
        //     glBindTexture(GL_TEXTURE_2D, NormalTex->GLTexID);
        // }

    }
    CheckGLError();
}

static
void RenderSkinnedMesh(shader_state *Shaders, skinned_mesh *Mesh, skeleton *Skel, mat4 &Projection) {
    for (u32 DrawIndex = 0; DrawIndex < Mesh->DrawCount; DrawIndex++) {
        //if (DrawIndex != 2) continue;
        skinned_mesh_draw *Draw = Mesh->Draws + DrawIndex;
        Assert(Draw->MaterialID > 0);
        Assert(Draw->MaterialID <= Mesh->MaterialCount);
        Assert(Draw->MeshID < Mesh->MeshCount);
        material *Material = Mesh->Materials + Draw->MaterialID-1;
        skinned_mesh_mesh *MeshData = Mesh->Meshes + Draw->MeshID;
        glBindVertexArray(MeshData->VaoID);

        // TODO: Fix bug in importer that causes 32 bone IDs
        if (Draw->NumBoneIDs == 0 || Draw->NumBoneIDs == 32) {
            glUseProgram(Shaders->TexProgramID);
            u32 ParentID = Draw->ParentBoneID;
            mat4 FullMatrix = Projection * mat4(Skel->WorldMatrices[ParentID]);
            glUniformMatrix4fv(Shaders->TexProjection, 1, GL_FALSE, &FullMatrix[0][0]);
            glUniform1i(Shaders->TexDiffuseTexture, 0);
        } else {
            u16 NumBones = Draw->NumBoneIDs;
            u16 NumWeights = MeshData->BoneCount;
            skin_shader *Shader = FindOrCreateSkinShader(Shaders, NumWeights, NumBones);
            Assert(Shader->ProgramID > 0);
            glUseProgram(Shader->ProgramID);
            glUniformMatrix4fv(Shader->Projection, 1, GL_FALSE, &Projection[0][0]);
            glUniform1i(Shader->DiffuseTexture, 0);

            mat4x3 BoneTransforms[MAX_BONES];
            for (u32 BoneIndex = 0; BoneIndex < NumBones; BoneIndex++) {
                BoneTransforms[BoneIndex] = Skel->WorldMatrices[Draw->BoneIDs[BoneIndex]];
            }
            glUniformMatrix4x3fv(Shader->Bones, NumBones, GL_FALSE, &BoneTransforms[0][0][0]);
        }

        glActiveTexture(GL_TEXTURE0);
        if (Material->DiffuseTexID == 0) {
            printf("Missing texture!");
            glBindTexture(GL_TEXTURE_2D, 0);
        } else {
            u32 GLTexID = Mesh->Textures[Material->DiffuseTexID-1].GLTexID;
            glBindTexture(GL_TEXTURE_2D, GLTexID);
        }

        glDrawElements(GL_TRIANGLES, Draw->MeshLength * 3, GL_UNSIGNED_SHORT, (void *)(Draw->MeshOffset * sizeof(u16) * 3));
    }
    CheckGLError();
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
        u32 PoseStart;
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

    FilePos = (char *)FileData + Pointers.PoseStart;
    Mesh->BindPose.BoneCount = *(u16 *) FilePos;
    FilePos += sizeof(u16);
    Mesh->BindPose.BoneParentIDs = (u16 *) FilePos;
    u32 Advance = (Mesh->BindPose.BoneCount | 1);
    FilePos += Advance * sizeof(u16);
    Mesh->BindPose.SetupPose = (transform *) FilePos;

    for (u32 c = 0; c < Mesh->BindPose.BoneCount; c++) {
        transform *BindPose = Mesh->BindPose.SetupPose + c;
        printf("%2d (%2d): [(%f %f %f) (%f %f %f %f) (%f %f %f)]\n", c, Mesh->BindPose.BoneParentIDs[c],
            BindPose->Translation.x, BindPose->Translation.y, BindPose->Translation.z,
            BindPose->Rotation.x, BindPose->Rotation.y, BindPose->Rotation.z, BindPose->Rotation.w,
            BindPose->Scale.x, BindPose->Scale.y, BindPose->Scale.z);

    }

    return Mesh;
}
