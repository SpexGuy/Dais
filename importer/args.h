//
// Created by Martin Wickham on 8/27/17.
//

#ifndef ARGS_H_
#define ARGS_H_

#define MAX_BLEND_WEIGHTS    8
#define MAX_DRAW_BONES      32

enum Mode {
    MODE_NONE,
    MODE_MODEL,
    MODE_ANIMATION,
};

struct Options {
    char *filepath = nullptr;
    char *outpath = nullptr;
    char *mapping = nullptr;

    Mode mode = MODE_NONE;

    int maxVertices = 32767;
    int maxDrawBones = 12;
    int maxBlendWeights = 4;
    bool flipV = false;
    bool packVertexColors = false;
    float animError = 0.0001;

    bool dumpElementTree = false;
    bool dumpObjectTree = false;
    bool dumpNodeTree = false;
    bool dumpMaterials = false;
    bool dumpMeshes = false;
    bool dumpGeom = false;
    bool dumpObj = false;
};

bool parseArgs(int argc, char *argv[], Options *opts);

#endif
