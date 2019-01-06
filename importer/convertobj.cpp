//
// Created by Martin Wickham on 8/27/17.
//

#include <cstdio>
#include "convertobj.h"

using namespace ofbx;

void convertFbxToObj(const IScene *scene, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        printf("Error: Could not dump obj to %s (can't fopen)\n", filename);
        return;
    }
    printf("Dumping OBJ to %s\n", filename);

    int obj_idx = 0;
    int mesh_count = scene->getMeshCount();
    for (int i = 0; i < mesh_count; ++i)
    {
        fprintf(fp, "o obj%d\ng grp%d\n", i, obj_idx);

        const Mesh& mesh = *scene->getMesh(i);
        const Geometry& geom = *mesh.getGeometry();
        int vertex_count = geom.getVertexCount();
        const ofbx::Vec3* vertices = geom.getVertices();
        for (int i = 0; i < vertex_count; ++i)
        {
            Vec3 v = vertices[i];
            fprintf(fp, "v %f %f %f\n", v.x, v.y, v.z);
        }

        bool has_normals = geom.getNormals() != nullptr;
        if (has_normals)
        {
            const ofbx::Vec3* normals = geom.getNormals();
            int count = geom.getVertexCount();

            for (int i = 0; i < count; ++i)
            {
                ofbx::Vec3 n = normals[i];
                fprintf(fp, "vn %f %f %f\n", n.x, n.y, n.z);
            }
        }

        bool has_uvs = geom.getUVs() != nullptr;
        if (has_uvs)
        {
            const ofbx::Vec2* uvs = geom.getUVs();
            int count = geom.getVertexCount();

            for (int i = 0; i < count; ++i)
            {
                ofbx::Vec2 uv = uvs[i];
                fprintf(fp, "vt %f %f\n", uv.x, uv.y);
            }
        }

        int countInFace = 0;
        int count = geom.getVertexCount();
        for (int i = 0; i < count; ++i)
        {
            if (countInFace == 0)
            {
                countInFace = 0;
                fputs("f ", fp);
            }
            int idx = i + 1;
            fprintf(fp, "%d", idx);

            if (has_normals)
            {
                fprintf(fp, "/%d", idx);
            }
            else
            {
                fprintf(fp, "/");
            }

            if (has_uvs)
            {
                fprintf(fp, "/%d", idx);
            }
            else
            {
                fprintf(fp, "/");
            }

            if (countInFace == 2) {
                fputc('\n', fp);
                countInFace = 0;
            } else {
                fputc(' ', fp);
                countInFace++;
            }
        }
        fputc('\n', fp);
        ++obj_idx;
    }

    fclose(fp);
}
