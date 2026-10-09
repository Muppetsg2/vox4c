/*
    vox2obj - bakes one animation frame of a .vox scene into a Wavefront OBJ + MTL.

    Shows the intended way of rebuilding a scene with vox4c: walk the nodes,
    skip invisible ones, pick the model active at the frame and place its mesh
    with v4c_node_model_matrix().

    usage: vox2obj in.vox out.obj [--frame N] [--scale S] [--coords yup|zup]
                                  [--all-materials] [--hidden]

    build: cc -std=c99 -O2 vox2obj.c -o vox2obj
*/
#define VOX4C_IMPLEMENTATION
#include "../vox4c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* strict argument parsing: the whole string must be a number */
static int parse_int_arg(const char* s, long min, long max, long* out)
{
    char* end = NULL;
    long  v;
    errno = 0;
    v     = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < min || v > max)
        return 0;

    *out = v;
    return 1;
}

static int parse_float_arg(const char* s, float* out)
{
    char*  end = NULL;
    double v;
    errno = 0;
    v     = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0' || !(v > 0.0) || v > 1.0e30)
        return 0;

    *out = (float)v;
    return 1;
}

static void xform_point(const float m[16], const float p[3], float out[3])
{
    int r;
    for (r = 0; r < 3; ++r)
        out[r] = m[0 * 4 + r] * p[0] + m[1 * 4 + r] * p[1] + m[2 * 4 + r] * p[2] + m[3 * 4 + r];
}

static void xform_dir(const float m[16], const float d[3], float out[3])
{
    int r;
    for (r = 0; r < 3; ++r)
        out[r] = m[0 * 4 + r] * d[0] + m[1 * 4 + r] * d[1] + m[2 * 4 + r] * d[2];
}

static float det3(const float m[16])
{
    return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) +
           m[8] * (m[1] * m[6] - m[5] * m[2]);
}

static const char* base_name(const char* path)
{
    const char* a = strrchr(path, '/');
    const char* b = strrchr(path, '\\');
    if (b > a)
        a = b;

    return a ? a + 1 : path;
}

int main(int argc, char** argv)
{
    v4c_load_options opt;
    v4c_scene*       s        = NULL;
    const char*      in_path  = NULL;
    const char*      out_path = NULL;
    char             mtl_path[1024];
    FILE*            obj;
    FILE*            mtl;
    uint32_t         frame = 0, n, i, base = 1, objects = 0, triangles = 0;
    int              include_hidden = 0, a;
    v4c_result       res;

    v4c_default_load_options(&opt);
    opt.flags        = V4C_FLAG_GENERATE_MESHES | V4C_FLAG_USED_MATERIALS_ONLY | V4C_FLAG_DISCARD_VOXELS;
    opt.coord_system = V4C_COORDS_Y_UP_RIGHT_HANDED; /* OBJ is conventionally Y-up */
    opt.scale        = 0.1f;

    for (a = 1; a < argc; ++a)
    {
        if (!strcmp(argv[a], "--frame") && a + 1 < argc)
        {
            long v;
            if (!parse_int_arg(argv[++a], 0, 0x7fffffffL, &v))
            {
                fprintf(stderr, "invalid frame '%s'\n", argv[a]);
                return 1;
            }
            frame = (uint32_t)v;
        }
        else if (!strcmp(argv[a], "--scale") && a + 1 < argc)
        {
            if (!parse_float_arg(argv[++a], &opt.scale))
            {
                fprintf(stderr, "invalid scale '%s' (must be > 0)\n", argv[a]);
                return 1;
            }
        }
        else if (!strcmp(argv[a], "--coords") && a + 1 < argc)
        {
            ++a;
            opt.coord_system = !strcmp(argv[a], "zup") ? V4C_COORDS_Z_UP_RIGHT_HANDED : V4C_COORDS_Y_UP_RIGHT_HANDED;
        }
        else if (!strcmp(argv[a], "--all-materials"))
        {
            opt.flags &= ~V4C_FLAG_USED_MATERIALS_ONLY;
        }
        else if (!strcmp(argv[a], "--hidden"))
        {
            include_hidden = 1;
        }
        else if (!in_path)
        {
            in_path = argv[a];
        }
        else if (!out_path)
        {
            out_path = argv[a];
        }
    }

    if (!in_path || !out_path)
    {
        fprintf(stderr,
                "usage: %s in.vox out.obj [--frame N] [--scale S] [--coords yup|zup] [--all-materials] [--hidden]\n",
                argv[0]);
        return 1;
    }

    /* Load only the requested frame: fails with V4C_ERROR_FRAME_NOT_FOUND
       when the animation is shorter, and drops models that are not visible
       at that frame, so only the needed meshes are generated. */
    opt.frame = (int32_t)frame;
    res       = v4c_load_file(in_path, &opt, &s);
    if (res != V4C_SUCCESS)
    {
        fprintf(stderr, "%s: %s\n", in_path, v4c_result_string(res));
        return 1;
    }

    snprintf(mtl_path, sizeof(mtl_path), "%s", out_path);
    {
        char* dot   = strrchr(mtl_path, '.');
        char* slash = strrchr(mtl_path, '/');
        if (dot && (!slash || dot > slash))
            *dot = 0;

        strncat(mtl_path, ".mtl", sizeof(mtl_path) - strlen(mtl_path) - 1);
    }

    obj = fopen(out_path, "w");
    mtl = fopen(mtl_path, "w");
    if (!obj || !mtl)
    {
        fprintf(stderr, "cannot write output files\n");
        if (obj)
            fclose(obj);

        if (mtl)
            fclose(mtl);

        v4c_free_scene(s);
        return 1;
    }

    /* ---- materials ---- */
    for (i = 0; i < s->num_materials; ++i)
    {
        const v4c_material* m = &s->materials[i];
        const float         r = m->color.r / 255.0f, g = m->color.g / 255.0f, b = m->color.b / 255.0f;
        fprintf(mtl, "newmtl mat_%u\n", m->palette_index);
        fprintf(mtl, "Kd %.4f %.4f %.4f\n", r, g, b);
        fprintf(mtl, "Pr %.4f\n", m->roughness);
        if (m->type == V4C_MATERIAL_METAL || m->type == V4C_MATERIAL_BLEND)
            fprintf(mtl, "Pm %.4f\n", m->metallic);

        if (m->type == V4C_MATERIAL_GLASS || m->type == V4C_MATERIAL_BLEND)
            fprintf(mtl, "d %.4f\nNi %.4f\n", 1.0f - m->transparency, m->ior);

        if (m->type == V4C_MATERIAL_MEDIA) /* OBJ has no volumes: a faint translucent surface */
            fprintf(mtl, "d %.4f\n", m->density < 1.0f ? m->density : 1.0f);

        if (m->type == V4C_MATERIAL_EMIT)
            fprintf(mtl, "Ke %.4f %.4f %.4f\n", r * m->emissive, g * m->emissive, b * m->emissive);

        fprintf(mtl, "\n");
    }

    fprintf(obj, "# %s, frame %u, exported with vox4c\nmtllib %s\n", base_name(in_path), frame, base_name(mtl_path));

    /* ---- scene ---- */
    for (n = 0; n < s->num_nodes; ++n)
    {
        const v4c_mesh* mesh;
        float           mat[16], p[3];
        const int32_t   model = v4c_sample_node_model(s, n, frame);
        int             flip;
        uint32_t        k, sm;

        if (model < 0 || (!include_hidden && !v4c_node_is_visible(s, n)))
            continue;

        mesh = &s->models[model].mesh;
        if (!mesh->num_indices)
            continue;

        v4c_node_model_matrix(s, n, frame, mat);
        flip = det3(mat) < 0.0f; /* mirrored instance: restore the winding */

        fprintf(obj, "o %s_%u\n", s->nodes[n].name ? s->nodes[n].name : "node", n);
        for (k = 0; k < mesh->num_vertices; ++k)
        {
            xform_point(mat, &mesh->positions[(size_t)k * 3], p);
            fprintf(obj, "v %g %g %g\n", p[0], p[1], p[2]);
        }

        for (k = 0; k < mesh->num_vertices; ++k)
        {
            xform_dir(mat, &mesh->normals[(size_t)k * 3], p);
            fprintf(obj, "vn %g %g %g\n", p[0], p[1], p[2]);
        }

        for (sm = 0; sm < mesh->num_submeshes; ++sm)
        {
            const v4c_submesh* sub = &mesh->submeshes[sm];
            fprintf(obj, "usemtl mat_%u\n", s->materials[sub->material].palette_index);
            for (k = sub->first_index; k < sub->first_index + sub->num_indices; k += 3)
            {
                const uint32_t i0 = mesh->indices[k] + base;
                uint32_t       i1 = mesh->indices[k + 1] + base, i2 = mesh->indices[k + 2] + base;
                if (flip)
                {
                    const uint32_t t = i1;
                    i1               = i2;
                    i2               = t;
                }
                fprintf(obj, "f %u//%u %u//%u %u//%u\n", i0, i0, i1, i1, i2, i2);
            }
        }
        base += mesh->num_vertices;
        triangles += mesh->num_indices / 3;
        ++objects;
    }

    fclose(obj);
    fclose(mtl);
    printf("%s -> %s (%u objects, %u triangles, %u materials)\n", in_path, out_path, objects, triangles,
           s->num_materials);
    v4c_free_scene(s);
    return 0;
}
