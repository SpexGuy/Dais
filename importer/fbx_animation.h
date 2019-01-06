#ifndef FBX_ANIMATION_H_
#define FBX_ANIMATION_H_

#include "openfbx/ofbx.h"
#include "animation_types.h"

animation *ConvertFBXToAnimation(const IScene *Scene, Options *Opts);

bool WriteAnimation(animation *Anim, const char *Filename);

#endif // FBX_ANIMATION_H
