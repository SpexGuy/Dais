#include "fbx_animation.h"

#define ANIM_TMP_MEM_SIZE Megabytes(16)

struct anim_state {
    memory_arena Arena;

    u16 NextNodeID;
    limb_node_info *Limbs;
};

static
u16 AppendLimbNode(anim_state *State, const Object *Node, u16 ParentID) {
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
void BuildSkeletonRecursive(anim_state *State, const Object *Node, u16 ParentID) {
    const Object *Child;
    for (int i = 0; (Child = Node->resolveObjectLink(i)); i++) {
        if (Child->isNode()) {
            u16 NodeID = ParentID;
            if (Child->getType() == Object::Type::LIMB_NODE) {
                NodeID = AppendLimbNode(State, Child, ParentID);
            }
            BuildSkeletonRecursive(State, Child, NodeID);
        }
    }
}

struct combine_times_data {
    memory_arena *Arena;
    u64 *Times;
    u32 BasePos;
    u32 Count;
};

struct combined_times {
    u32 Count;
    u64 *Times;
};

// merge the array of times in the given curve with the times in the
// aggregate data.  If the data is uninitialized, will make a copy of
// the times in the curve.  If the curve is null, this has no effect.
// After combining, this function moves the resulting array on top of
// the original array in combine_times_data.  The arena allocator in
// the combine_times_data should be considered reserved while a
// combine_times_data instance is in use.
static
void CombineTimes(combine_times_data *Data, const AnimationCurve *Curve) {
    if (Curve) {
        u32 CurveCount = Curve->getKeyCount();
        const u64 *CurveTimes = Curve->getKeyTime();
        if (!Data->Times) {
            // set up initial state
            Data->BasePos = Data->Arena->Pos;
            Data->Times = (u64 *) ArenaCopy(Data->Arena, CurveTimes, CurveCount * sizeof(u64));
            Data->Count = CurveCount;

        } else {
            u32 DataCount = Data->Count;
            u64 *DataTimes = Data->Times;

            // reserve enough memory for zero collisions
            u32 OutCount = 0;
            u64 *OutTimes = ArenaAllocTN(Data->Arena, u64, DataCount + CurveCount);

            // merge arrays until one ends
            u32 DataPos = 0;
            u32 CurvePos = 0;
            while (DataPos < DataCount && CurvePos < CurveCount) {
                u64 CurveTime = CurveTimes[CurvePos];
                u64 DataTime = DataTimes[DataPos];
                if (CurveTime < DataTime) {
                    OutTimes[OutCount++] = CurveTime;
                    CurvePos++;
                } else if (CurveTime > DataTime) {
                    OutTimes[OutCount++] = DataTime;
                    DataPos++;
                } else {
                    OutTimes[OutCount++] = CurveTime;
                    CurvePos++;
                    DataPos++;
                }
            }

            // consume the remaining arrays. At most one of these
            // loops will actually execute.
            while (DataPos < DataCount) {
                OutTimes[OutCount++] = DataTimes[DataPos++];
            }
            while (CurvePos < CurveCount) {
                OutTimes[OutCount++] = CurveTimes[CurvePos++];
            }

            // move the resulting array on top of the original array
            // and then restore sanity to the arena
            u32 SizeBytes = OutCount * sizeof(u64);
            memmove(DataTimes, OutTimes, SizeBytes);
            ArenaRestore(Data->Arena, Data->BasePos + SizeBytes);
        }
    }
}

static
combined_times ComputeSampleTimes(anim_state *State, const AnimationCurveNode *Channel) {
    combine_times_data CombinedTimes = {};
    CombinedTimes.Arena = &State->Arena;

    if (Channel) {
        const AnimationCurve *X = Channel->getCurve(0);
        const AnimationCurve *Y = Channel->getCurve(1);
        const AnimationCurve *Z = Channel->getCurve(2);
        CombineTimes(&CombinedTimes, X);
        CombineTimes(&CombinedTimes, Y);
        CombineTimes(&CombinedTimes, Z);
    }

    return (combined_times){CombinedTimes.Count, CombinedTimes.Times};
}

struct sample_state {
    memory_arena *Arena;
    const Object *FbxNode;
    const AnimationCurveNode *Translation;
    const AnimationCurveNode *Rotation;
    const AnimationCurveNode *Scale;
    Vec3 LocalTranslation;
    Vec3 LocalRotation;
    Vec3 LocalScale;
    double StartTime;
    double Timespan;
};

typedef void extract_channel(const Matrix *Mat, float *Out);

template<typename value>
static
void SampleAnimation(
    sample_state *State,
    combined_times Samples,
    timeline<value> *Timeline,
    extract_channel *Extract)
{
    Timeline->KeyframeCount = Samples.Count;
    if (Samples.Count) {
        const Object *FbxNode = State->FbxNode;
        const AnimationCurveNode *Translation = State->Translation;
        const AnimationCurveNode *Rotation = State->Rotation;
        const AnimationCurveNode *Scale = State->Scale;
        Vec3 LocalTranslation = State->LocalTranslation;
        Vec3 LocalRotation = State->LocalRotation;
        Vec3 LocalScale = State->LocalScale;

        Timeline->Percentages = ArenaAllocTN(State->Arena, f32, Samples.Count);
        Timeline->Values = ArenaAllocTN(State->Arena, value, Samples.Count);
        for (u32 SampleIndex = 0; SampleIndex < Samples.Count; SampleIndex++) {
            u64 FbxTime = Samples.Times[SampleIndex];
            double Seconds = fbxTimeToSeconds(FbxTime);
            Vec3 Trn = (!Translation) ? LocalTranslation : Translation->getNodeLocalTransform(Seconds, LocalTranslation);
            Vec3 Rot = (!Rotation) ? LocalRotation : Rotation->getNodeLocalTransform(Seconds, LocalRotation);
            Vec3 Scl = (!Scale) ? LocalScale : Scale->getNodeLocalTransform(Seconds, LocalScale);
            Matrix LocalMatrix = FbxNode->evalLocal(Trn, Rot, Scl);
            Timeline->Percentages[SampleIndex] = (f32) ((Seconds - State->StartTime) / State->Timespan);
            Extract(&LocalMatrix, (float *) (Timeline->Values + SampleIndex));
        }
    }
}


animation *ConvertFBXToAnimation(const IScene *Scene, Options *Opts) {
    u32 AnimationCount = Scene->getAnimationStackCount();
    if (AnimationCount == 0) {
        printf("Error: No animations found in the source fbx file.\n");
        exit(0);
    }

    if (AnimationCount != 1) {
        printf("Warning: multiple animations found! Ignoring all but the first.\n");
    }

    const AnimationStack *Stack = Scene->getAnimationStack(0);
    const AnimationLayer *Layer = Stack->getLayer(0);

    printf("Converting animation: %s\n", Stack->name);
    const TakeInfo *Take = Scene->getTakeInfo(Stack->name);
    if (!Take) {
        printf("Error: Take info not found!\n");
        exit(0);
    }

    double StartTime = Take->local_time_from;
    double Timespan = Take->local_time_to - StartTime;
    if (Timespan < 0) {
        printf("Error: Negative timespan!\n");
        exit(0);
    }

    anim_state State = {};
    ArenaInit(&State.Arena, calloc(ANIM_TMP_MEM_SIZE, 1), ANIM_TMP_MEM_SIZE);
    animation *Result = ArenaAllocT(&State.Arena, animation);

    bone_assignment Assignment = ReadBoneAssignment(Opts->mapping, &State.Arena);

    const Object *Root = Scene->getRoot();
    BuildSkeletonRecursive(&State, Root, 0);
    u32 LimbCount = ReverseLinkedList((void **) &State.Limbs);

    Result->Duration = (f32) Timespan;
    Result->Bones = ArenaAllocTN(&State.Arena, bone_animation, LimbCount);
    bone_animation *NextAnim = Result->Bones;
    limb_node_info *Node = State.Limbs;
    while (Node) {
        const Object *FbxNode = Node->Source;
        const AnimationCurveNode *Translation = Layer->getCurveNode(*FbxNode, "Lcl Translation");
        const AnimationCurveNode *Rotation = Layer->getCurveNode(*FbxNode, "Lcl Rotation");
        const AnimationCurveNode *Scale = Layer->getCurveNode(*FbxNode, "Lcl Scaling");
        combined_times TranslationSamples = ComputeSampleTimes(&State, Translation);
        combined_times RotationSamples = ComputeSampleTimes(&State, Rotation);
        combined_times ScaleSamples = ComputeSampleTimes(&State, Scale);

        sample_state SampleState = {};
        SampleState.Arena = &State.Arena;
        SampleState.FbxNode = FbxNode;
        SampleState.Translation = Translation;
        SampleState.Rotation = Rotation;
        SampleState.Scale = Scale;
        SampleState.LocalTranslation = FbxNode->getLocalTranslation();
        SampleState.LocalRotation = FbxNode->getLocalRotation();
        SampleState.LocalScale = FbxNode->getLocalScaling();
        SampleState.StartTime = StartTime;
        SampleState.Timespan = Timespan;


        u16 Flags = 0;
        if (TranslationSamples.Count) {
            Flags |= CHANNEL_FLAG_TRANSLATION;
        }
        if (RotationSamples.Count) {
            Flags |= CHANNEL_FLAG_ROTATION;  
        }
        if (ScaleSamples.Count) {
            Flags |= CHANNEL_FLAG_SCALE;
        }
        if (Flags) {
            NextAnim->BoneID = FindBoneIDByName(&Assignment, FbxNode->name);
            NextAnim->ChannelFlags = Flags;
            SampleAnimation(&SampleState, TranslationSamples, &NextAnim->Translations, ExtractTranslation);
            SampleAnimation(&SampleState, RotationSamples, &NextAnim->Rotations, ExtractRotation);
            SampleAnimation(&SampleState, ScaleSamples, &NextAnim->Scales, ExtractScale);

            // printf("Bone %d [%s]\n", Node->ID, FbxNode->name);
            // for (u32 c = 0; c < NextAnim->Translations.KeyframeCount; c++) {
            //     printf("T %f [%f %f %f]\n",
            //         NextAnim->Translations.Percentages[c],
            //         NextAnim->Translations.Values[c].x,
            //         NextAnim->Translations.Values[c].y,
            //         NextAnim->Translations.Values[c].z);
            // }
            // for (u32 c = 0; c < NextAnim->Rotations.KeyframeCount; c++) {
            //     printf("R %f [%f %f %f %f]\n",
            //         NextAnim->Rotations.Percentages[c],
            //         NextAnim->Rotations.Values[c].x,
            //         NextAnim->Rotations.Values[c].y,
            //         NextAnim->Rotations.Values[c].z,
            //         NextAnim->Rotations.Values[c].w);
            // }
            // for (u32 c = 0; c < NextAnim->Scales.KeyframeCount; c++) {
            //     printf("S %f [%f %f %f]\n",
            //         NextAnim->Scales.Percentages[c],
            //         NextAnim->Scales.Values[c].x,
            //         NextAnim->Scales.Values[c].y,
            //         NextAnim->Scales.Values[c].z);
            // }
            // printf("\n");

            NextAnim++;
            Result->AnimatedBoneCount++;
        }
        Node = Node->Next;
    }

    printf("Animated %d/%d bones\n", Result->AnimatedBoneCount, LimbCount);
    return Result;
}

bool WriteAnimation(animation *Anim, const char *Filename) {
    FILE *File = fopen(Filename, "wb");
    if (!File) {
        return false;
    }

    u32 Pad = 0;

    fwrite(&Pad, 1, sizeof(u32), File);
    fwrite(&Pad, 1, sizeof(u32), File);
    fwrite(&Anim->Duration, 1, sizeof(f32), File);
    fwrite(&Anim->AnimatedBoneCount, 1, sizeof(u16), File);
    fwrite(&Pad, 1, sizeof(u16), File);

    for (u32 BoneIndex = 0; BoneIndex < Anim->AnimatedBoneCount; BoneIndex++) {
        bone_animation *Bone = Anim->Bones + BoneIndex;
        fwrite(&Bone->BoneID, 1, sizeof(u16), File);
        fwrite(&Bone->Translations.KeyframeCount, 1, sizeof(u16), File);
        fwrite(&Bone->Rotations.KeyframeCount, 1, sizeof(u16), File);
        fwrite(&Bone->Scales.KeyframeCount, 1, sizeof(u16), File);
    }

    u32 PercentStart = ftell(File);
    for (u32 BoneIndex = 0; BoneIndex < Anim->AnimatedBoneCount; BoneIndex++) {
        bone_animation *Bone = Anim->Bones + BoneIndex;
        if (Bone->Translations.KeyframeCount) {
            fwrite(Bone->Translations.Percentages,
                Bone->Translations.KeyframeCount,
                sizeof(f32), File);
        }
        if (Bone->Rotations.KeyframeCount) {
            fwrite(Bone->Rotations.Percentages,
                Bone->Rotations.KeyframeCount,
                sizeof(f32), File);
        }
        if (Bone->Scales.KeyframeCount) {
            fwrite(Bone->Scales.Percentages,
                Bone->Scales.KeyframeCount,
                sizeof(f32), File);
        }
    }

    u32 DataStart = ftell(File);
    for (u32 BoneIndex = 0; BoneIndex < Anim->AnimatedBoneCount; BoneIndex++) {
        bone_animation *Bone = Anim->Bones + BoneIndex;
        if (Bone->Translations.KeyframeCount) {
            fwrite(Bone->Translations.Values,
                Bone->Translations.KeyframeCount,
                sizeof(v3), File);
        }
        if (Bone->Rotations.KeyframeCount) {
            fwrite(Bone->Rotations.Values,
                Bone->Rotations.KeyframeCount,
                sizeof(quat), File);
        }
        if (Bone->Scales.KeyframeCount) {
            fwrite(Bone->Scales.Values,
                Bone->Scales.KeyframeCount,
                sizeof(v3), File);
        }
    }

    fseek(File, 0, SEEK_SET);
    fwrite(&PercentStart, 1, sizeof(u32), File);
    fwrite(&DataStart, 1, sizeof(u32), File);

    return true;
}
