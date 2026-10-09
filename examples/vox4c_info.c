/*
    vox4c_info - prints everything vox4c reads from a .vox file.

    usage: vox4c_info file.vox [--frame N] [--compact] [--mesh]
                               [--coords zup|unreal|yup|unity] [--scale S]

    build: cc -std=c99 -O2 vox4c_info.c -o vox4c_info
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

static const char* type_name(v4c_material_type t)
{
    switch (t)
    {
    case V4C_MATERIAL_METAL: return "metal";
    case V4C_MATERIAL_GLASS: return "glass";
    case V4C_MATERIAL_EMIT: return "emit";
    case V4C_MATERIAL_BLEND: return "blend";
    case V4C_MATERIAL_MEDIA: return "media";
    default: return "diffuse";
    }
}

static const char* media_name(v4c_media_type t)
{
    switch (t)
    {
    case V4C_MEDIA_SCATTER: return "scatter";
    case V4C_MEDIA_EMIT: return "emit";
    case V4C_MEDIA_SSS: return "sss";
    default: return "absorb";
    }
}

static const char* axis_name(v4c_axis a)
{
    static const char* names[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    return (unsigned)a < 6u ? names[a] : "?";
}

static void print_node(const v4c_scene* s, uint32_t index, int depth)
{
    const v4c_node* n = &s->nodes[index];
    uint32_t        i;
    printf("%*s[%u] %s", depth * 2, "", index, n->name ? n->name : "(unnamed)");
    if (n->layer >= 0 && (uint32_t)n->layer < s->num_layers)
        printf("  layer=%d", n->layer);

    if (n->hidden)
        printf("  hidden");

    if (!v4c_node_is_visible(s, index))
        printf("  (invisible)");

    printf("\n");

    for (i = 0; i < n->num_transform_keys; ++i)
    {
        const v4c_transform* t = &n->transform_keys[i].transform;
        printf("%*s  key f=%-3u t=(%g, %g, %g) q=(%.3f, %.3f, %.3f, %.3f) s=(%g, %g, %g)%s\n", depth * 2, "",
               n->transform_keys[i].frame, t->translation[0], t->translation[1], t->translation[2], t->quaternion[0],
               t->quaternion[1], t->quaternion[2], t->quaternion[3], t->scale[0], t->scale[1], t->scale[2],
               (i == 0 && n->transform_loop && n->num_transform_keys > 1) ? "  [loop]" : "");
    }

    for (i = 0; i < n->num_model_keys; ++i)
        printf("%*s  model f=%-3u -> model %u%s\n", depth * 2, "", n->model_keys[i].frame, n->model_keys[i].model,
               (i == 0 && n->model_loop && n->num_model_keys > 1) ? "  [loop]" : "");

    for (i = 0; i < n->num_children; ++i)
        print_node(s, n->children[i], depth + 1);
}

int main(int argc, char** argv)
{
    v4c_load_options opt;
    v4c_scene*       s    = NULL;
    const char*      path = NULL;
    v4c_result       res;
    uint32_t         i;
    int              a;

    v4c_default_load_options(&opt);
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
            opt.frame = (int32_t)v;
        }
        else if (!strcmp(argv[a], "--compact"))
        {
            opt.flags |= V4C_FLAG_USED_MATERIALS_ONLY;
        }
        else if (!strcmp(argv[a], "--mesh"))
        {
            opt.flags |= V4C_FLAG_GENERATE_MESHES;
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
            const char* c = argv[++a];
            if (!strcmp(c, "zup"))
            {
                opt.coord_system = V4C_COORDS_MAGICAVOXEL;
            }
            else if (!strcmp(c, "unreal"))
            {
                opt.coord_system = V4C_COORDS_UNREAL;
            }
            else if (!strcmp(c, "yup"))
            {
                opt.coord_system = V4C_COORDS_OPENGL;
            }
            else if (!strcmp(c, "unity"))
            {
                opt.coord_system = V4C_COORDS_UNITY;
            }
            else
            {
                fprintf(stderr, "unknown coordinate system '%s'\n", c);
                return 1;
            }
        }
        else if (argv[a][0] != '-')
        {
            path = argv[a];
        }
        else
        {
            fprintf(stderr, "unknown option '%s'\n", argv[a]);
            return 1;
        }
    }

    if (!path)
    {
        fprintf(stderr,
                "usage: %s file.vox [--frame N] [--compact] [--mesh] [--coords zup|unreal|yup|unity] [--scale S]\n",
                argv[0]);
        return 1;
    }

    res = v4c_load_file(path, &opt, &s);
    if (res != V4C_SUCCESS)
    {
        fprintf(stderr, "%s: %s\n", path, v4c_result_string(res));
        return 1;
    }

    printf("file version %u, %u frame(s), loaded frame: ", s->file_version, s->num_frames);
    if (s->loaded_frame == V4C_ALL_FRAMES)
        printf("all\n");
    else
        printf("%d\n", s->loaded_frame);
    printf("axes: out = (%s, %s, %s) of MagicaVoxel, %s handed, scale %g\n", axis_name(s->axes[0]),
           axis_name(s->axes[1]), axis_name(s->axes[2]), (int)s->left_handed ? "left" : "right", s->scale);

    printf("\n== %u layer(s)\n", s->num_layers);
    for (i = 0; i < s->num_layers; ++i)
        printf("  [%u] id=%d '%s' color=#%02x%02x%02x%s\n", i, s->layers[i].id,
               s->layers[i].name ? s->layers[i].name : "", s->layers[i].color.r, s->layers[i].color.g,
               s->layers[i].color.b, (int)s->layers[i].hidden ? " hidden" : "");

    printf("\n== %u model(s)\n", s->num_models);
    for (i = 0; i < s->num_models; ++i)
    {
        const v4c_model* m = &s->models[i];
        printf("  [%u] file #%u  %ux%ux%u  %u voxels  pivot=(%g, %g, %g)", i, m->file_index, m->size[0], m->size[1],
               m->size[2], m->num_voxels, m->pivot[0], m->pivot[1], m->pivot[2]);
        if (m->mesh.num_indices)
            printf("  mesh: %u verts, %u tris, %u submeshes", m->mesh.num_vertices, m->mesh.num_indices / 3,
                   m->mesh.num_submeshes);
        printf("\n");
    }

    printf("\n== %u material(s)%s\n", s->num_materials,
           (opt.flags & V4C_FLAG_USED_MATERIALS_ONLY) ? " (used only)" : "");
    for (i = 0; i < s->num_materials; ++i)
    {
        const v4c_material* m = &s->materials[i];
        /* in full mode print only non-diffuse materials to keep the output short */
        if (!(opt.flags & V4C_FLAG_USED_MATERIALS_ONLY) && m->type == V4C_MATERIAL_DIFFUSE)
            continue;

        printf("  [%3u] palette %3u #%02x%02x%02x%02x %-7s rough=%g metal=%g spec=%g ior=%g trans=%g", i,
               m->palette_index, m->color.r, m->color.g, m->color.b, m->color.a, type_name(m->type), m->roughness,
               m->metallic, m->specular, m->ior, m->transparency);
        if (m->type == V4C_MATERIAL_EMIT)
            printf(" emit=%g flux=%g ldr=%g", m->emissive, m->emission_power, m->ldr);

        if (m->type == V4C_MATERIAL_GLASS || m->type == V4C_MATERIAL_BLEND || m->type == V4C_MATERIAL_MEDIA)
            printf(" media=%s density=%g phase=%g", media_name(m->media_type), m->density, m->phase);

        if (m->plastic)
            printf(" plastic");

        printf("\n");
    }

    if (!(opt.flags & V4C_FLAG_USED_MATERIALS_ONLY))
        printf("  (diffuse materials omitted, use --compact to list the used ones)\n");

    /* ---- render settings and cameras ---- */
    {
        const v4c_render_settings* r   = &s->render;
        const v4c_sun*             sun = &r->sun;
        static const char* modes[]     = { "perspective", "free", "panorama", "orthographic", "isometric", "unknown" };
        printf("\n== render settings\n");
        if (r->has_background)
            printf("  background  #%02x%02x%02x%s\n", r->background.r, r->background.g, r->background.b,
                   (int)r->show_background ? " (shown in the render)" : " (the render shows the sky)");

        if (r->has_ambient)
            printf("  ambient     #%02x%02x%02x intensity %g\n", r->ambient_color.r, r->ambient_color.g,
                   r->ambient_color.b, r->ambient_intensity);

        if (r->has_lens)
        {
            static const char* lenses[] = { "perspective", "stereographic", "panorama", "unknown" };
            printf("  lens        %s (%d), fov %g, aperture %g\n", lenses[r->projection], r->projection_index, r->fov,
                   r->aperture);
        }

        if (sun->present)
        {
            printf(
                "  sun         %s, #%02x%02x%02x intensity %g, elevation %g, azimuth %g, direction (%.3f, %.3f, %.3f)\n",
                sun->type == V4C_LIGHT_AREA ? "area" : "directional", sun->color.r, sun->color.g, sun->color.b,
                sun->intensity, sun->elevation, sun->azimuth, sun->direction[0], sun->direction[1], sun->direction[2]);

            if (sun->type == V4C_LIGHT_AREA)
                printf("              %g deg wide -> disk at (%.1f, %.1f, %.1f), radius %g, power %g\n",
                       sun->angular_diameter, sun->area_light.position[0], sun->area_light.position[1],
                       sun->area_light.position[2], sun->area_light.radius, sun->area_light.power);
        }

        for (i = 0; i < s->num_cameras; ++i)
        {
            const v4c_camera* cam = &s->cameras[i];
            float             pos[3], fwd[3];
            if (!cam->saved)
            {
                printf("  camera %u    (empty slot)\n", cam->id);
                continue;
            }
            v4c_camera_pose(s, cam, pos, fwd, NULL);
            printf("  camera %u    %s fov %g, at (%.2f, %.2f, %.2f) looking (%.3f, %.3f, %.3f)\n", cam->id,
                   modes[cam->mode], cam->fov, pos[0], pos[1], pos[2], fwd[0], fwd[1], fwd[2]);
        }
    }

    printf("\n== %u node(s)\n", s->num_nodes);
    if (s->num_nodes)
        print_node(s, 0, 1);

    v4c_free_scene(s);
    return 0;
}
