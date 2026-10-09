/*
    vox4c_memory - compares the mesh memory of float vertices and packed
    vertices (V4C_FLAG_PACKED_VERTICES) and prints a Markdown table row per
    file, ready to paste into the README.

    usage: vox4c_memory [--compact] file.vox [more.vox ...]

    build: cc -std=c99 -O2 vox4c_memory.c -o vox4c_memory

    Counted bytes (payload of the arrays, allocator overhead not included):
      full   vertices: positions 12 + normals 12 + texcoords 8 + material 1 = 33 B
      packed vertices: sizeof(v4c_packed_vertex)                            =  8 B
      both:            indices 4 B each + submeshes 12 B each
*/
#define VOX4C_IMPLEMENTATION
#include "../vox4c.h"

#include <stdio.h>
#include <string.h>

typedef struct mesh_bytes
{
    uint64_t vertices;  /* number of vertices          */
    uint64_t triangles; /* number of triangles         */
    uint64_t vertex;    /* bytes of vertex data        */
    uint64_t index;     /* bytes of indices            */
    uint64_t submesh;   /* bytes of submesh records    */
} mesh_bytes;

static void count_scene(const v4c_scene* s, mesh_bytes* out)
{
    uint32_t i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < s->num_models; ++i)
    {
        const v4c_mesh* m = &s->models[i].mesh;
        uint64_t        per_vertex;
        if (m->packed)
            per_vertex = sizeof(v4c_packed_vertex);
        else
            per_vertex = 3 * sizeof(float) /* positions */ + 3 * sizeof(float) /* normals */ +
                         2 * sizeof(float) /* texcoords */ + sizeof(uint8_t) /* materials */;

        out->vertices += m->num_vertices;
        out->triangles += m->num_indices / 3u;
        out->vertex += per_vertex * m->num_vertices;
        out->index += sizeof(uint32_t) * (uint64_t)m->num_indices;
        out->submesh += sizeof(v4c_submesh) * (uint64_t)m->num_submeshes;
    }
}

static void print_bytes(uint64_t b)
{
    if (b >= (uint64_t)1024u * 1024u)
        printf("%.2f MB", (double)b / (1024.0 * 1024.0));
    else
        printf("%.1f KB", (double)b / 1024.0);
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
    uint32_t extra = 0;
    int      a, printed_header = 0, errors = 0;

    for (a = 1; a < argc; ++a)
    {
        v4c_load_options opt;
        v4c_scene*       full   = NULL;
        v4c_scene*       packed = NULL;
        v4c_result       r1, r2;
        mesh_bytes       f, p;
        uint64_t         total_f, total_p;

        if (!strcmp(argv[a], "--compact"))
        {
            extra |= V4C_FLAG_USED_MATERIALS_ONLY;
            continue;
        }

        v4c_default_load_options(&opt);
        opt.flags = V4C_FLAG_GENERATE_MESHES | V4C_FLAG_DISCARD_VOXELS | extra;
        r1        = v4c_load_file(argv[a], &opt, &full);
        opt.flags = V4C_FLAG_PACKED_VERTICES | V4C_FLAG_DISCARD_VOXELS | extra;
        r2        = v4c_load_file(argv[a], &opt, &packed);
        if (r1 != V4C_SUCCESS || r2 != V4C_SUCCESS)
        {
            fprintf(stderr, "%s: %s\n", argv[a], v4c_result_string(r1 != V4C_SUCCESS ? r1 : r2));
            v4c_free_scene(full);
            v4c_free_scene(packed);
            ++errors;
            continue;
        }

        count_scene(full, &f);
        count_scene(packed, &p);
        total_f = f.vertex + f.index + f.submesh;
        total_p = p.vertex + p.index + p.submesh;

        if (!printed_header)
        {
            printf("| file | vertices | triangles | vertex data (full) | vertex data (packed) | indices | "
                   "total (full) | total (packed) | saving |\n");
            printf("|---|---:|---:|---:|---:|---:|---:|---:|---:|\n");
            printed_header = 1;
        }
        printf("| %s | %llu | %llu | ", base_name(argv[a]), (unsigned long long)f.vertices,
               (unsigned long long)f.triangles);
        print_bytes(f.vertex);
        printf(" | ");
        print_bytes(p.vertex);
        printf(" | ");
        print_bytes(f.index);
        printf(" | ");
        print_bytes(total_f);
        printf(" | ");
        print_bytes(total_p);
        printf(" | %.2fx |\n", total_p ? (double)total_f / (double)total_p : 0.0);

        v4c_free_scene(full);
        v4c_free_scene(packed);
    }

    if (!printed_header && !errors)
    {
        fprintf(stderr, "usage: %s [--compact] file.vox [more.vox ...]\n", argv[0]);
        return 1;
    }
    return errors ? 1 : 0;
}
