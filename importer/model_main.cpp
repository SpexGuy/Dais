#include <time.h>

Options opts;

int main(int argc, char *argv[]) {
    clock_t start = clock();
    if (!parseArgs(argc, argv, &opts)) {
        exit(1);
    }

    printf("Converting '%s' to '%s'\n", opts.filepath, opts.outpath);

    // load the file into memory
    FILE* fp = fopen(opts.filepath, "rb");
    if (!fp) {
        printf("Failed to open file '%s'\n", opts.filepath);
        exit(-1);
    }
    fseek(fp, 0, SEEK_END);
    size_t file_size = size_t(ftell(fp));
    fseek(fp, 0, SEEK_SET);
    auto* content = new u8[file_size];
    fread(content, 1, file_size, fp);
    fclose(fp);

    // parse fbx into a usable format
    ofbx::IScene *scene = ofbx::load(content, file_size);
    const char *error = ofbx::getError();
    if (!scene || (error && error[0])) {
        printf("Failed to parse fbx file '%s'\n", opts.filepath);
        if (error && error[0]) printf("OpenFBX Error: %s\n", error);
        exit(-2);
    }

    // print out the fbx tree
    if (opts.dumpElementTree) {
        dumpElements(scene);
    }

    if (opts.dumpObjectTree) {
        dumpObjects(scene);
    }

    if (opts.dumpNodeTree) {
        dumpNodes(scene);
    }

    if (opts.dumpObj) {
        convertFbxToObj(scene, "geom.obj");
    }

    if (opts.mode == MODE_MODEL) {
        skinned_mesh_with_pose *Skinned = ConvertFBXToSkinnedMesh(&opts, scene);

        bool Written = WriteMesh(Skinned, opts.outpath);
        if (!Written) {
            printf("Failed to write output mesh %s", opts.outpath);
            exit(-2);
        }

        Written = WriteBoneAssignment(&Skinned->BoneNames, opts.mapping);
        if (!Written) {
            printf("Failed to write mapping file %s", opts.mapping);
            exit(-2);
        }
    }

    if (opts.mode == MODE_ANIMATION) {
        animation *Anim = ConvertFBXToAnimation(scene, &opts);

        bool Written = WriteAnimation(Anim, opts.outpath);
        if (!Written) {
            printf("Failed to write output mesh %s", opts.outpath);
            exit(-2);
        }
    }

    clock_t end = clock();
    printf("Completed in %dms\n", int((end - start) * 1000 / CLOCKS_PER_SEC));
    exit(0); // die before piecewise deallocating.

    scene->destroy();
}
