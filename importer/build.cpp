#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-W#pragma-messages"
extern "C" {
#include "openfbx/miniz.c"
}
#include "openfbx/ofbx.cpp"
#pragma clang diagnostic pop

#include "arena.cpp"
#include "args.cpp"
#include "animation.cpp"
#include "dumpfbx.cpp"
#include "convertobj.cpp"
#include "fbx_skinned_mesh.cpp"
#include "fbx_animation.cpp"
#include "model_main.cpp"
