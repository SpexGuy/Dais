//
// Created by Martin Wickham on 8/27/17.
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "args.h"

static void printHelp(const char *programName) {
    printf("Usage: %s [options] filename mapping [outfile]\n", programName);
    printf("Options:\n");
    printf("  -s            convert a [s]kinned mesh to an skm file\n");
    printf("  -a            convert an [a]nimation to a ska file\n");
    printf("  -m maxVerts   limit the max number of vertices in a [m]esh (default 32768)\n");
    printf("  -b maxBones   limit the max number of [b]ones in a draw call (default 12)\n");
    printf("  -w maxWeights limit the max number of bone [w]eights per vertex (default 4)\n");
    printf("  -f            [f]lip the V texture axis\n");
    printf("  -p            [p]ack vertex colors into 4 bytes\n");
    printf("  -h or -?      display this [h]elp message and exit\n");
    printf("\n");
    printf("Debugging Options:\n");
    printf("  -d [flags]    dump intermediate data. Possible flags are:\n");
    printf("                  o  dump the fbx [o]bject tree to the file 'objects.out'\n");
    printf("                  e  dump the fbx [e]lement tree to the file 'elements.out'\n");
    printf("                  n  dump the fbx [n]ode tree to the file 'nodes.out'\n");
    printf("                  m  dump the fbx [m]aterials to the console\n");
    printf("                  M  dump the fbx [M]eshes to the console\n");
    printf("                  g  dump the fbx [g]eometry to the console\n");
    printf("                  O  dump a .obj file containing the tesselated geometry to 'geom.obj'\n");
}

bool parseArgs(int argc, char *argv[], Options *opts) {
    if (argc <= 0) { // JIC
        printHelp("model_build");
        return false;
    }
    if (argc == 1) {
        printHelp(argv[0]);
        return false;
    }

    bool success = true; // If set to false, print the help when done parsing and exit.
    char mode = 0; // The arg we're currently processing

    int current = 1; // The current index in argv
    char *cc = nullptr; // The current arg. By convention, cc being nullptr causes the next arg to be fetched
    while (current < argc) {
        bool inlineArg = false; // If set to true, the next iteration of the loop will use the same arg offset by 2 characters.
        char nextmode = 0;      // The mode to use in the next iteration of the loop
        if (!cc) cc = argv[current++]; // Fetch the next arg

        switch (mode) {

            case 'm': {
                int maxVerts = atoi(cc);
                if (maxVerts == 0 && cc[0] != '0') {
                    printf("Error: couldn't parse '%s' as integer for argument -m\n", cc);
                    goto parseError;
                } else if (maxVerts <= 3) {
                    printf("Error: max vertices must be at least 3, lest there be no triangles. (%d requested)\n", maxVerts);
                    success = false;
                } else if (maxVerts > 65536) {
                    printf("Error: max vertices per mesh cannot be more than 65536, since indices are 16 bits. (%d requested)\n", maxVerts);
                    success = false;
                } else {
                    if (maxVerts < 100) printf("Warning: vertices per mesh restricted to %d. This may lead to excess draw calls and decreased performance.\n", maxVerts);
                    if (maxVerts > 32768) printf("Warning: meshes may be larger than 32768 vertices. This may lead to large indices appearing negative from java code.\n");
                    opts->maxVertices = maxVerts;
                }
                break;
            }

            case 'b': {
                int bones = atoi(cc);
                if (bones == 0 && cc[0] != '0') {
                    printf("Error: couldn't parse '%s' as integer for argument -b\n", cc);
                    goto parseError;
                } else if (bones < 0 || bones > MAX_DRAW_BONES) {
                    printf("Error: number of draw bones must be between 0 and %d (%d requested)\n", MAX_DRAW_BONES, bones);
                    success = false;
                } else {
                    opts->maxDrawBones = bones;
                    if (bones == 0) printf("Disabling vertex skinning because of '-b 0' argument.\n");
                }
                break;
            }

            case 'w': {
                int weights = atoi(cc);
                if (weights == 0 && cc[0] != '0') {
                    printf("Error: couldn't parse '%s' as integer for argument -w\n", cc);
                    goto parseError;
                } else if (weights < 0 || weights > MAX_BLEND_WEIGHTS) {
                    printf("Error: number of blend weights must be between 0 and %d (%d requested)\n", MAX_BLEND_WEIGHTS, weights);
                    success = false;
                } else {
                    opts->maxBlendWeights = weights;
                    if (weights == 0) printf("Disabling vertex skinning because of '-w 0' argument.");
                }
                break;
            }

            case 'd': {
                while (*cc) {
                    switch (*cc) {
                    case 'o':
                        opts->dumpObjectTree = true;
                        break;
                    case 'e':
                        opts->dumpElementTree = true;
                        break;
                    case 'n':
                        opts->dumpNodeTree = true;
                        break;
                    case 'm':
                        opts->dumpMaterials = true;
                        break;
                    case 'M':
                        opts->dumpMeshes = true;
                        break;
                    case 'g':
                        opts->dumpGeom = true;
                        break;
                    case 'O':
                        opts->dumpObj = true;
                        break;
                    default:
                        printf("Ignoring unknown debug flag: '%c' (%d)\n", *cc, *cc);
                        break;
                    }
                    cc++;
                }
                break;
            }

            default:
                printf("Unknown flag: '-%c'\n", mode);
            parseError:
                success = false;
                // intentional fall through
            case 0:
                if (cc[0] == '-') {
                    nextmode = cc[1];
                    inlineArg = nextmode && cc[2];
                } else if (opts->filepath == nullptr) {
                    opts->filepath = cc;
                } else if (opts->mapping == nullptr) {
                    opts->mapping = cc;
                } else if (opts->outpath == nullptr) {
                    opts->outpath = cc;
                } else {
                    printf("Unknown argument: %s\n", cc);
                    success = false;
                }
                break;

        }

        // handle unary args here
        switch (nextmode) {
        case 's':
            if (opts->mode != MODE_NONE && opts->mode != MODE_MODEL) {
                printf("Multiple import types specified! Please specify only one of -a, -s\n");
                success = false;
            }
            opts->mode = MODE_MODEL;
            break;
        case 'a':
            if (opts->mode != MODE_NONE && opts->mode != MODE_ANIMATION) {
                printf("Multiple import types specified! Please specify only one of -a, -s\n");
                success = false;
            }
            opts->mode = MODE_ANIMATION;
            break;
        case 'f':
            opts->flipV = true;
            break;
        case 'p':
            opts->packVertexColors = true;
            break;
        case 'h':
        case '?':
            printHelp(argv[0]);
            exit(0);
            break;
        default:
            mode = nextmode;
            if (inlineArg) {
                cc += 2;
                continue;
            }
        }

        cc = nullptr;
    }

    if (mode != 0) {
        printf("Expected another parameter after '-%c'\n", mode);
        success = false;
    }

    if (opts->filepath == nullptr) {
        printf("You must specify an input file and a mapping file.\n");
        success = false;
    } else if (opts->mapping == nullptr) {
        printf("You must specify a mapping file.\n");
        success = false;
    }

    if (opts->mode == MODE_NONE) {
        printf("You must specify one of -a, -s.\n");
        success = false;
    }

    if (!success) {
        printHelp(argv[0]);
    } else {
        if (!opts->outpath) {
            char *lastDot = strrchr(opts->filepath, '.');

            size_t pos;
            if (lastDot == nullptr) pos = strlen(opts->filepath);
            else pos = lastDot - opts->filepath;

            const char *ext = mode == MODE_MODEL ? ".skm" : ".ska";
            int extLen = strlen(ext);
            opts->outpath = new char[pos + extLen + 1];
            strncpy(opts->outpath, opts->filepath, pos);
            strncpy(opts->outpath + pos, ext, extLen + 1);
        }

        if (opts->maxBlendWeights == 0 || opts->maxDrawBones == 0) {
            opts->maxBlendWeights = 0;
            opts->maxDrawBones = 0;
        }

    }

    return success;
}
