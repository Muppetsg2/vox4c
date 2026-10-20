/*
    vox4c_anim - plays the animation of a .vox scene the way an engine would.

    Shows the animation API of vox4c:
      * the editor's timeline (v4c_scene::anim_first .. anim_last, META _anim_range),
      * v4c_frame_instances(): the models drawn at a frame with their matrices,
        at fractional frames and with MagicaVoxel or smooth sampling,
      * meshes on demand: the scene is loaded with voxels only and the meshes of
        the models a frame needs are built with v4c_build_model_mesh() and freed
        when no frame needs them any more,
      * v4c_bake_frame(): one frame as a single mesh in world space (OBJ export).

    usage: vox4c_anim in.vox [--step S] [--smooth] [--obj prefix]
             --step S    frame step (default 1, e.g. 0.25 for four samples per frame)
             --smooth    smooth sampling (default: MagicaVoxel's)
             --obj P     writes every sampled frame to P_<frame>.obj

    build: cc -std=c99 -O2 vox4c_anim.c -o vox4c_anim
*/
#define VOX4C_IMPLEMENTATION
#include "../vox4c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAX_INSTANCES 4096u

static int parse_step(const char* s, float* out)
{
    char*  end = NULL;
    double v;
    errno = 0;
    v     = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0' || !(v >= 0.01) || v > 1000.0)
        return 0;

    *out = (float)v;
    return 1;
}

/* bytes of a model mesh as it is stored (packed vertices, 16 or 32-bit indices) */
static size_t mesh_bytes(const v4c_mesh* m)
{
    const size_t vertex = m->packed ? sizeof(v4c_packed_vertex) : (size_t)(3 + 3 + 2) * sizeof(float) + 1u;
    const size_t index  = m->indices16 ? sizeof(uint16_t) : sizeof(uint32_t);
    return (size_t)m->num_vertices * vertex + (size_t)m->num_indices * index +
           (size_t)m->num_submeshes * sizeof(v4c_submesh);
}

static int write_obj(const char* path, const v4c_mesh* m)
{
    FILE*    f = fopen(path, "w");
    uint32_t i, k;
    if (!f)
        return 0;

    fprintf(f, "# vox4c_anim: %u vertices, %u triangles\n", m->num_vertices, m->num_indices / 3u);
    for (i = 0; i < m->num_vertices; ++i)
    {
        const float* p = &m->positions[(size_t)i * 3u];
        fprintf(f, "v %g %g %g\n", p[0], p[1], p[2]);
    }

    for (i = 0; i < m->num_vertices; ++i)
    {
        const float* n = &m->normals[(size_t)i * 3u];
        fprintf(f, "vn %g %g %g\n", n[0], n[1], n[2]);
    }

    for (k = 0; k < m->num_submeshes; ++k)
    {
        const v4c_submesh* sm = &m->submeshes[k];
        fprintf(f, "g material_%u\n", (unsigned)sm->material);
        for (i = sm->first_index; i + 2u < sm->first_index + sm->num_indices; i += 3u)
        {
            const uint32_t a = v4c_mesh_index(m, i) + 1u, b = v4c_mesh_index(m, i + 1u) + 1u;
            const uint32_t c = v4c_mesh_index(m, i + 2u) + 1u;
            fprintf(f, "f %u//%u %u//%u %u//%u\n", a, a, b, b, c, c);
        }
    }
    return fclose(f) == 0;
}

int main(int argc, char** argv)
{
    const char*      in         = NULL;
    const char*      obj_prefix = NULL;
    float            step       = 1.0f;
    v4c_sampling     sampling   = V4C_SAMPLE_MAGICAVOXEL;
    v4c_load_options opt;
    v4c_scene*       scene = NULL;
    v4c_result       r;
    v4c_instance*    inst        = NULL;
    unsigned char*   used        = NULL; /* models the current frame needs */
    size_t           eager_bytes = 0, resident = 0, peak = 0;
    uint32_t         i, built = 0, sample, num_samples;
    int              a, ok    = 1;

    for (a = 1; a < argc; ++a)
    {
        if (strcmp(argv[a], "--step") == 0 && a + 1 < argc)
        {
            if (!parse_step(argv[++a], &step))
            {
                fprintf(stderr, "invalid --step: %s\n", argv[a]);
                return 1;
            }
        }
        else if (strcmp(argv[a], "--smooth") == 0)
        {
            sampling = V4C_SAMPLE_SMOOTH;
        }
        else if (strcmp(argv[a], "--obj") == 0 && a + 1 < argc)
        {
            obj_prefix = argv[++a];
        }
        else if (!in && argv[a][0] != '-')
        {
            in = argv[a];
        }
        else
        {
            fprintf(stderr, "unknown argument: %s\n", argv[a]);
            return 1;
        }
    }

    if (!in)
    {
        fprintf(stderr, "usage: vox4c_anim in.vox [--step S] [--smooth] [--obj prefix]\n");
        return 1;
    }

    /* voxels only: no meshes yet */
    v4c_default_load_options(&opt);
    opt.coord_system = V4C_COORDS_Y_UP_RIGHT_HANDED;
    r                = v4c_load_file(in, &opt, &scene);
    if (r != V4C_SUCCESS)
    {
        fprintf(stderr, "%s: %s\n", in, v4c_result_string(r));
        return 1;
    }

    printf("%s\n  models %u, nodes %u, keyframes 0 .. %u, timeline %u .. %u, %s sampling, step %g\n", in,
           scene->num_models, scene->num_nodes, scene->num_frames - 1u, scene->anim_first, scene->anim_last,
           sampling == V4C_SAMPLE_SMOOTH ? "smooth" : "MagicaVoxel", (double)step);

    /* what meshing everything up front would cost (packed vertices, 32-bit indices) */
    for (i = 0; i < scene->num_models; ++i)
    {
        if (v4c_build_model_mesh(scene, i, V4C_FLAG_PACKED_VERTICES) != V4C_SUCCESS)
        {
            fprintf(stderr, "out of memory\n");
            v4c_free_scene(scene);
            return 1;
        }
        eager_bytes += mesh_bytes(&scene->models[i].mesh);
        v4c_free_model_mesh(scene, i);
    }

    inst = (v4c_instance*)malloc(MAX_INSTANCES * sizeof(v4c_instance));
    used = (unsigned char*)calloc(scene->num_models ? scene->num_models : 1u, 1);
    if (!inst || !used)
    {
        fprintf(stderr, "out of memory\n");
        free(inst);
        free(used);
        v4c_free_scene(scene);
        return 1;
    }

    /* the timeline, sampled every `step` frames */
    num_samples = 1u + (uint32_t)((float)(scene->anim_last - scene->anim_first) / step + 0.0001f);
    for (sample = 0; ok && sample < num_samples; ++sample)
    {
        const float frame = (float)scene->anim_first + step * (float)sample;
        uint32_t    n     = v4c_frame_instances(scene, frame, sampling, true, inst, MAX_INSTANCES);
        uint32_t    k, tris = 0;
        if (n > MAX_INSTANCES)
            n = MAX_INSTANCES;

        /* build the meshes this frame needs, free the ones it does not */
        memset(used, 0, scene->num_models);
        for (k = 0; k < n; ++k)
            used[inst[k].model] = 1;

        for (i = 0; i < scene->num_models; ++i)
        {
            v4c_mesh* m = &scene->models[i].mesh;
            if (used[i] && m->num_vertices == 0 && scene->models[i].num_voxels > 0)
            {
                if (v4c_build_model_mesh(scene, i, V4C_FLAG_PACKED_VERTICES | V4C_FLAG_16BIT_INDICES) != V4C_SUCCESS)
                {
                    fprintf(stderr, "out of memory\n");
                    ok = 0;
                    break;
                }
                resident += mesh_bytes(m);
                ++built;
            }
            else if (!used[i] && m->num_vertices > 0)
            {
                resident -= mesh_bytes(m);
                v4c_free_model_mesh(scene, i);
            }
        }

        if (resident > peak)
            peak = resident;

        /* an engine would now draw scene->models[inst[k].model].mesh with inst[k].matrix
           (and reversed culling for inst[k].mirrored) */
        for (k = 0; k < n; ++k)
            tris += scene->models[inst[k].model].mesh.num_indices / 3u;

        printf("  frame %7.2f: %u instance(s), %u triangles", (double)frame, n, tris);
        for (k = 0; k < n && k < 4u; ++k)
            printf("%s model %u%s", k ? "," : "  ", inst[k].model, (int)inst[k].mirrored ? " (mirrored)" : "");
        printf(n > 4u ? ", ...\n" : "\n");

        if (obj_prefix && ok)
        {
            v4c_mesh baked;
            char     path[1024];
            r = v4c_bake_frame(scene, frame, sampling, true, 0, &baked);
            if (r != V4C_SUCCESS)
            {
                fprintf(stderr, "v4c_bake_frame: %s\n", v4c_result_string(r));
                ok = 0;
            }
            else
            {
                snprintf(path, sizeof(path), "%s_%07.2f.obj", obj_prefix, (double)frame);
                if (!write_obj(path, &baked))
                {
                    fprintf(stderr, "cannot write %s\n", path);
                    ok = 0;
                }
                v4c_free_mesh(&baked);
            }
        }
    }

    printf("  mesh memory: all %u models up front %zu bytes, on demand at most %zu bytes (%u meshes built)\n",
           scene->num_models, eager_bytes, peak, built);

    free(inst);
    free(used);
    v4c_free_scene(scene);
    return ok ? 0 : 1;
}
