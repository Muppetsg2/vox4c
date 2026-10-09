/*
    vox4c_tests
      cc -std=c99 -Wall -Wextra -I.. vox4c_tests.c -o vox4c_tests
      ./vox4c_tests file1.vox file2.vox ...

    For every file and every coordinate system preset it checks that:
      * the mesh surface area equals the number of exposed voxel faces
        (no holes, no overlapping triangles),
      * the signed volume equals the number of voxels (all triangles face
        outwards with the requested winding),
      * every normal agrees with its triangle's winding,
      * "used materials only" produces the same geometry with remapped
        materials that point at the same palette colours,
      * single frame loading matches sampling the full animation,
      * truncated / corrupted input never crashes,
      * packed vertices decode bit-exactly to the float vertex arrays.
    Independent of the files it also checks every MATL combination written
    by MagicaVoxel (all material types, media types and parameters).
*/
/* built twice by CMake: with the implementation compiled in here, and against
   the static library vox4c::lib (which defines VOX4C_PREBUILT) */
#ifndef VOX4C_PREBUILT
#define VOX4C_IMPLEMENTATION
#endif
#include "../vox4c.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;
#define CHECK(cond, ...)                                  \
    do                                                    \
    {                                                     \
        if (!(cond))                                      \
        {                                                 \
            ++g_failures;                                 \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

#define MAX_FILE_SIZE ((size_t)1024 * 1024 * 256)

static unsigned char* read_file(const char* path, size_t* size)
{
    FILE*          f = fopen(path, "rb");
    unsigned char* buf;
    long           raw_len;
    size_t         file_len;

    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }

    raw_len = ftell(f);

    if (raw_len < 0)
    {
        fclose(f);
        return NULL;
    }

    file_len = (size_t)raw_len;

    if (file_len > MAX_FILE_SIZE)
    {
        fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return NULL;
    }

    buf = (unsigned char*)malloc(file_len > 0 ? file_len : 1);

    if (!buf)
    {
        fclose(f);
        return NULL;
    }

    if (file_len > 0 && fread(buf, 1, file_len, f) != file_len)
    {
        free(buf);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *size = file_len;
    return buf;
}

static int voxel_at(const v4c_model* m, const unsigned char* grid, int x, int y, int z)
{
    if (x < 0 || y < 0 || z < 0 || x >= (int)m->size[0] || y >= (int)m->size[1] || z >= (int)m->size[2])
        return 0;

    return grid[(uint32_t)x + m->size[0] * ((uint32_t)y + m->size[1] * (uint32_t)z)];
}

static void check_model(const v4c_scene* s, const v4c_model* m, float scale, int clockwise)
{
    unsigned char*   grid;
    size_t           exposed = 0;
    uint32_t         i;
    double           area = 0.0, volume = 0.0;
    static const int dirs[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

    if (!m->voxels)
        return;

    grid = (unsigned char*)calloc((size_t)m->size[0] * m->size[1] * m->size[2] + 1, 1);

    if (!grid)
        return;

    for (i = 0; i < m->num_voxels; ++i)
    {
        const v4c_voxel* v = &m->voxels[i];
        CHECK(v->x < m->size[0] && v->y < m->size[1] && v->z < m->size[2], "voxel out of bounds");
        CHECK(v->material < s->num_materials, "voxel material out of range");
        grid[v->x + m->size[0] * (v->y + m->size[1] * v->z)] = 1;
    }

    for (i = 0; i < m->num_voxels; ++i)
    {
        int k;
        for (k = 0; k < 6; ++k)
            if (!voxel_at(m, grid, m->voxels[i].x + dirs[k][0], m->voxels[i].y + dirs[k][1],
                          m->voxels[i].z + dirs[k][2]))
                ++exposed;
    }

    {
        const v4c_mesh* me        = &m->mesh;
        uint32_t        sub_total = 0;
        CHECK(me->num_indices % 3 == 0, "index count");
        for (i = 0; i < me->num_submeshes; ++i)
        {
            uint32_t j;
            CHECK(me->submeshes[i].first_index == sub_total, "submesh ranges must be contiguous");
            sub_total += me->submeshes[i].num_indices;
            for (j = me->submeshes[i].first_index; j < me->submeshes[i].first_index + me->submeshes[i].num_indices; ++j)
                CHECK(me->materials[me->indices[j]] == me->submeshes[i].material,
                      "vertex material != submesh material");
        }

        CHECK(sub_total == me->num_indices, "submeshes do not cover all indices");
        for (i = 0; i < me->num_indices; i += 3)
        {
            const float* a     = &me->positions[(size_t)me->indices[i + 0] * 3];
            const float* b     = &me->positions[(size_t)me->indices[i + 1] * 3];
            const float* c     = &me->positions[(size_t)me->indices[i + 2] * 3];
            const float* n     = &me->normals[(size_t)me->indices[i] * 3];
            const double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            const double e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            const double cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                   e1[0] * e2[1] - e1[1] * e2[0] };
            const double len   = sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
            const double dot   = cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2];
            area += 0.5 * len;
            volume += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) +
                       a[2] * (b[0] * c[1] - b[1] * c[0])) /
                      6.0;
            CHECK(clockwise ? dot < 0 : dot > 0, "triangle winding disagrees with normal");
            CHECK(me->indices[i] < me->num_vertices && me->indices[i + 1] < me->num_vertices &&
                      me->indices[i + 2] < me->num_vertices,
                  "index out of range");
        }

        {
            const double expect_area = (double)exposed * scale * scale;
            const double expect_vol  = (double)m->num_voxels * scale * scale * scale * (clockwise ? -1.0 : 1.0);
            CHECK(fabs(area - expect_area) < 1e-3 * (expect_area + 1), "area %.3f != exposed faces %.3f", area,
                  expect_area);
            CHECK(fabs(volume - expect_vol) < 1e-3 * (fabs(expect_vol) + 1), "signed volume %.3f != %.3f", volume,
                  expect_vol);
        }
    }
    free(grid);
}

/* exact equality on purpose: both sides come from the same arithmetic */
static int floats_equal(const float* a, const float* b, int n)
{
    int i;
    for (i = 0; i < n; ++i)
        if (a[i] != b[i]) /* NOLINT(clang-diagnostic-float-equal) */
            return 0;

    return 1;
}

/* Packed vertices must decode to exactly the float arrays of an unpacked load. */
/* NOLINTNEXTLINE(readability-function-size): a test case, long by nature */
static void compare_packed(const unsigned char* data, size_t size, const v4c_load_options* base)
{
    v4c_load_options opt  = *base;
    v4c_scene *      full = NULL, *packed = NULL;
    uint32_t         i, k;

    opt.flags = (base->flags | V4C_FLAG_GENERATE_MESHES) & ~V4C_FLAG_PACKED_VERTICES;
    CHECK(v4c_load_memory(data, size, &opt, &full) == V4C_SUCCESS, "packed: full load");
    /* V4C_FLAG_PACKED_VERTICES alone must imply mesh generation */
    opt.flags = (base->flags & ~V4C_FLAG_GENERATE_MESHES) | V4C_FLAG_PACKED_VERTICES;
    CHECK(v4c_load_memory(data, size, &opt, &packed) == V4C_SUCCESS, "packed: packed load");
    if (full && packed && full->num_models == packed->num_models)
    {
        for (i = 0; i < full->num_models; ++i)
        {
            const v4c_model* fm = &full->models[i];
            const v4c_model* pm = &packed->models[i];
            const v4c_mesh*  f  = &fm->mesh;
            const v4c_mesh*  p  = &pm->mesh;
            CHECK(f->num_vertices == p->num_vertices && f->num_indices == p->num_indices &&
                      f->num_submeshes == p->num_submeshes,
                  "packed: counts differ (model %u)", i);
            if (f->num_vertices != p->num_vertices || f->num_indices != p->num_indices ||
                f->num_submeshes != p->num_submeshes)
                continue;

            CHECK(f->packed == NULL, "packed: full mesh has packed data");
            if (p->num_vertices)
                CHECK(p->packed && !p->positions && !p->normals && !p->texcoords && !p->materials,
                      "packed: wrong arrays allocated");

            CHECK(p->num_indices == 0 || memcmp(f->indices, p->indices, p->num_indices * sizeof(uint32_t)) == 0,
                  "packed: indices differ");
            CHECK(p->num_submeshes == 0 ||
                      memcmp(f->submeshes, p->submeshes, p->num_submeshes * sizeof(v4c_submesh)) == 0,
                  "packed: submeshes differ");
            for (k = 0; k < p->num_vertices && p->packed; ++k)
            {
                const v4c_packed_vertex* pv = &p->packed[k];
                float                    pos[3], nrm[3], uv[2], pos2[3], nrm2[3], uv2[2];
                uint32_t                 mat;
                CHECK(pv->x <= pm->size[0] && pv->y <= pm->size[1] && pv->z <= pm->size[2] && pv->normal < 6,
                      "packed: vertex out of range");
                mat = v4c_mesh_vertex(packed, pm, k, pos, nrm, uv);
                v4c_unpack_vertex(packed, pm, pv, pos2, nrm2, uv2);
                if (!floats_equal(pos, &f->positions[(size_t)k * 3], 3) ||
                    !floats_equal(nrm, &f->normals[(size_t)k * 3], 3) ||
                    !floats_equal(uv, &f->texcoords[(size_t)k * 2], 2) || mat != f->materials[k] ||
                    !floats_equal(pos, pos2, 3) || !floats_equal(nrm, nrm2, 3) || !floats_equal(uv, uv2, 2))
                {
                    CHECK(0, "packed: vertex %u of model %u decodes differently", k, i);
                    break;
                }

                /* the generic accessor must also work on the unpacked mesh */
                mat = v4c_mesh_vertex(full, fm, k, pos2, nrm2, uv2);
                if (!floats_equal(pos, pos2, 3) || !floats_equal(nrm, nrm2, 3) || mat != f->materials[k])
                {
                    CHECK(0, "v4c_mesh_vertex on full mesh");
                    break;
                }
            }
        }
    }
    v4c_free_scene(full);
    v4c_free_scene(packed);
}

/* NOLINTNEXTLINE(readability-function-size): a test case, long by nature */
static void test_file(const char* path)
{
    size_t         size = 0;
    unsigned char* data = read_file(path, &size);
    int            cs;
    printf("%s\n", path);
    if (!data)
    {
        CHECK(0, "cannot read %s", path);
        return;
    }

    /* ---- geometry invariants in every coordinate system ---- */
    for (cs = 0; cs < 4; ++cs)
    {
        int cw;
        for (cw = 0; cw < 2; ++cw)
        {
            v4c_load_options opt;
            v4c_scene*       s = NULL;
            v4c_result       r;
            uint32_t         i;
            v4c_default_load_options(&opt);
            opt.coord_system = (v4c_coord_system)cs;
            opt.flags        = V4C_FLAG_GENERATE_MESHES | (cw ? V4C_FLAG_CLOCKWISE_WINDING : 0);
            opt.scale        = cs == 1 ? 10.0f : 1.0f;
            opt.pivot        = (v4c_pivot)((cs + cw) % 4);
            r                = v4c_load_memory(data, size, &opt, &s);
            CHECK(r == V4C_SUCCESS, "load failed: %s", v4c_result_string(r));
            if (r != V4C_SUCCESS)
                continue;

            CHECK(s->left_handed == (cs == 1 || cs == 3), "handedness");
            for (i = 0; i < s->num_models; ++i)
                check_model(s, &s->models[i], opt.scale, cw);

            compare_packed(data, size, &opt);
            if (cw)
            {
                v4c_load_options o2 = opt;
                o2.flags |= V4C_FLAG_USED_MATERIALS_ONLY | V4C_FLAG_DISCARD_VOXELS;
                compare_packed(data, size, &o2);
            }

            if (cs == 0 && cw == 0)
            {
                uint32_t tris = 0, verts = 0, vox = 0;
                for (i = 0; i < s->num_models; ++i)
                {
                    tris += s->models[i].mesh.num_indices / 3;
                    verts += s->models[i].mesh.num_vertices;
                    vox += s->models[i].num_voxels;
                }
                printf("  models %u  nodes %u  layers %u  frames %u  voxels %u  -> %u tris, %u verts\n", s->num_models,
                       s->num_nodes, s->num_layers, s->num_frames, vox, tris, verts);
            }
            v4c_free_scene(s);
        }
    }

    /* ---- used materials only ---- */
    {
        v4c_load_options opt;
        v4c_scene *      full = NULL, *compact = NULL;
        uint32_t         i, j;
        v4c_default_load_options(&opt);
        opt.flags = V4C_FLAG_GENERATE_MESHES;
        v4c_load_memory(data, size, &opt, &full);
        opt.flags |= V4C_FLAG_USED_MATERIALS_ONLY;
        v4c_load_memory(data, size, &opt, &compact);
        CHECK(full && compact, "load");
        if (full && compact)
        {
            CHECK(full->num_materials == 256, "full palette must have 256 materials");
            CHECK(compact->num_materials <= 255, "compact palette too large");
            for (i = 0; i < 256; ++i)
                CHECK(full->materials[i].palette_index == i, "full palette index");

            for (i = 0; i < full->num_models; ++i)
            {
                const v4c_model* a = &full->models[i];
                const v4c_model* b = &compact->models[i];
                CHECK(a->num_voxels == b->num_voxels, "voxel counts differ");
                CHECK(a->mesh.num_indices == b->mesh.num_indices, "index counts differ");
                for (j = 0; j < a->num_voxels && j < b->num_voxels; ++j)
                {
                    const v4c_material* ma = &full->materials[a->voxels[j].material];
                    const v4c_material* mb = &compact->materials[b->voxels[j].material];
                    CHECK(ma->palette_index == mb->palette_index && ma->type == mb->type &&
                              memcmp(&ma->color, &mb->color, 4) == 0 && ma->roughness == mb->roughness,
                          "material mismatch");
                }
            }

            /* every compact material is used */
            {
                unsigned char used[256] = { 0 };
                for (i = 0; i < compact->num_models; ++i)
                    for (j = 0; j < compact->models[i].num_voxels; ++j)
                        used[compact->models[i].voxels[j].material] = 1;
                for (i = 0; i < compact->num_materials; ++i)
                    CHECK(used[i], "unused material %u in compact palette", i);
            }
        }
        v4c_free_scene(full);
        v4c_free_scene(compact);
    }

    /* ---- single frame == sampled animation ---- */
    {
        v4c_load_options opt;
        v4c_scene*       all = NULL;
        uint32_t         f;
        v4c_default_load_options(&opt);
        opt.coord_system = V4C_COORDS_Y_UP_LEFT_HANDED;
        opt.scale        = 0.5f;
        v4c_load_memory(data, size, &opt, &all);
        CHECK(all != NULL, "load");
        for (f = 0; all && f < all->num_frames + 1; ++f)
        {
            v4c_scene* one = NULL;
            v4c_result r;
            uint32_t   n;
            opt.frame = (int32_t)f;
            r         = v4c_load_memory(data, size, &opt, &one);
            if (f == all->num_frames)
            {
                CHECK(r == V4C_ERROR_FRAME_NOT_FOUND, "frame past the end must fail");
                continue;
            }

            CHECK(r == V4C_SUCCESS, "single frame load");
            if (!one)
                continue;

            CHECK(one->num_nodes == all->num_nodes, "node count differs");
            for (n = 0; n < one->num_nodes && n < all->num_nodes; ++n)
            {
                float         wa[16], wb[16];
                const int32_t ma = v4c_sample_node_model(all, n, f), mb = v4c_sample_node_model(one, n, f);
                int           k;
                v4c_node_world_matrix(all, n, f, wa);
                v4c_node_world_matrix(one, n, 12345, wb); /* single frame scene ignores the frame */
                for (k = 0; k < 16; ++k)
                    CHECK(fabsf(wa[k] - wb[k]) < 1e-4f, "world matrix differs at frame %u node %u", f, n);

                CHECK((ma < 0) == (mb < 0), "model presence differs");
                if (ma >= 0 && mb >= 0)
                    CHECK(all->models[ma].file_index == one->models[mb].file_index, "sampled model differs");

                CHECK(one->nodes[n].num_transform_keys == 1, "single frame must have one key");
            }
            v4c_free_scene(one);
        }
        v4c_free_scene(all);
    }

    /* ---- quaternion/scale reproduce the rotation matrix ---- */
    {
        v4c_scene* s = NULL;
        uint32_t   n, k;
        v4c_load_memory(data, size, NULL, &s);
        for (n = 0; s && n < s->num_nodes; ++n)
            for (k = 0; k < s->nodes[n].num_transform_keys; ++k)
            {
                const v4c_transform* t = &s->nodes[n].transform_keys[k].transform;
                const float x = t->quaternion[0], y = t->quaternion[1], z = t->quaternion[2], w = t->quaternion[3];
                const float q[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                                     2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                                     2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y) };

                int r, c;
                for (r = 0; r < 3; ++r)
                    for (c = 0; c < 3; ++c)
                        CHECK(fabsf(q[r * 3 + c] * t->scale[c] - t->rotation[r * 3 + c]) < 1e-5f,
                              "quaternion * scale != rotation");
            }
        v4c_free_scene(s);
    }

    /* ---- robustness: truncation and random corruption ---- */
    {
        size_t           cut;
        unsigned         seed = 12345u;
        unsigned char*   copy = (unsigned char*)malloc(size);
        v4c_load_options opt;
        v4c_default_load_options(&opt);
        opt.flags = V4C_FLAG_GENERATE_MESHES | V4C_FLAG_USED_MATERIALS_ONLY;
        for (cut = 0; cut < size; cut += 1 + size / 97)
        {
            v4c_scene* s = NULL;
            v4c_load_memory(data, cut, &opt, &s);
            v4c_free_scene(s);
        }

        for (cut = 0; size > 0 && cut < 60; ++cut)
        {
            v4c_scene* s = NULL;
            int        k;
            memcpy(copy, data, size);
            for (k = 0; k < 8; ++k)
            {
                seed                     = seed * 1103515245u + 12345u;
                copy[(seed >> 8) % size] = (unsigned char)(seed >> 3);
            }
            v4c_load_memory(copy, size, &opt, &s);
            v4c_free_scene(s);
        }
        free(copy);
    }
    free(data);
}

/* ---- materials: every MATL combination MagicaVoxel 0.99.7 writes ---- */
/* Key/value lists extracted from a real file (ids 1..46). */
static const char* k_matl_dump[46] = {
    /* _media */
    "_type=_media;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_media;_media_type=_scatter;_media=1;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_media;_media_type=_emit;_media=2;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_media;_media_type=_sss;_media=3;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_media;_media_type=_emit;_media=2;_rough=0.1;_ior=0.3;_ri=1.3;_g=0.03;_d=0.049",
    "_type=_media;_media_type=_scatter;_media=1;_rough=0.1;_ior=0.3;_ri=1.3;_g=0.04;_d=0.051",
    "_type=_media;_media_type=_sss;_media=3;_rough=0.1;_ior=0.3;_ri=1.3;_g=0.9;_d=0.055",
    "_type=_media;_rough=0.1;_ior=0.3;_ri=1.3;_g=0.02;_d=0.052",
    /* _blend */
    "_type=_blend;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_blend;_media_type=_scatter;_media=1;_alpha=0.01;_trans=0.01;_metal=0.01;_rough=0.11;_ior=0.31;_ri=1.31;_sp=1.1;_g=0.01;_d=0.051",
    "_type=_blend;_media_type=_emit;_media=2;_alpha=0.01;_trans=0.01;_metal=0.01;_rough=0.11;_ior=0.31;_ri=1.31;_sp=1.01;_g=0.01;_d=0.051",
    "_type=_blend;_media_type=_sss;_media=3;_alpha=0.01;_trans=0.01;_metal=0.01;_rough=0.11;_ior=0.31;_ri=1.31;_sp=1.01;_g=0.01;_d=0.051",
    "_type=_blend;_media_type=_scatter;_media=1;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_blend;_media_type=_emit;_media=2;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_blend;_media_type=_sss;_media=3;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_blend;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    /* _emit */
    "_type=_emit;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_emit;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05;_emit=0.01;_flux=2;_ldr=0.01",
    "_type=_emit;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05;_ldr=1", "_type=_emit;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05;_flux=4",
    "_type=_emit;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05;_emit=1", "_type=_emit;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_emit;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05", "_type=_emit;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    /* _glass */
    "_type=_glass;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_glass;_media_type=_scatter;_media=1;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_glass;_media_type=_emit;_media=2;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_glass;_media_type=_sss;_media=3;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_glass;_alpha=0.01;_trans=0.01;_rough=0.11;_ior=0.31;_ri=1.31;_g=0.01;_d=0.051",
    "_type=_glass;_media_type=_scatter;_media=1;_alpha=0.01;_trans=0.01;_rough=0.11;_ior=0.31;_ri=1.31;_g=0.01;_d=0.051",
    "_type=_glass;_media_type=_emit;_media=2;_alpha=0.01;_trans=0.01;_rough=0.11;_ior=0.31;_ri=1.31;_g=0.01;_d=0.051",
    "_type=_glass;_media_type=_sss;_media=3;_alpha=0.01;_trans=0.01;_rough=0.11;_ior=0.31;_ri=1.31;_g=0.01;_d=0.051",
    /* _metal */
    "_type=_metal;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_metal;_metal=0.01;_rough=0.11;_ior=0.31;_ri=1.31;_sp=1.01;_d=0.05",
    "_type=_metal;_rough=1;_ior=0.3;_ri=1.3;_d=0.05", "_type=_metal;_rough=0.1;_ior=2;_ri=3;_d=0.05",
    "_type=_metal;_rough=0.1;_ior=0.3;_ri=1.3;_sp=2;_d=0.05",
    "_type=_metal;_metal=1;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_type=_metal;_metal=1;_rough=1;_ior=2;_ri=3;_sp=2;_d=0.05", "_type=_metal;_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    /* diffuse: no _type key at all */
    "_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05", "_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05", "_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05",
    "_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05", "_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05", "_rough=0.1;_ior=0.3;_ri=1.3;_d=0.05"
};

typedef struct buf_t
{
    unsigned char* p;
    size_t         n, cap;
} buf_t;

static void put(buf_t* b, const void* d, size_t n)
{
    if (b->n + n > b->cap)
    {
        unsigned char* grown;
        b->cap = (b->n + n) * 2;
        grown  = (unsigned char*)realloc(b->p, b->cap);
        if (!grown)
        {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        b->p = grown;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void put_i32(buf_t* b, int32_t v)
{
    unsigned char c[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16),
                           (unsigned char)(v >> 24) };
    put(b, c, 4);
}

static void put_str(buf_t* b, const char* str, size_t n)
{
    put_i32(b, (int32_t)n);
    put(b, str, n);
}

static void put_chunk(buf_t* b, const char* id, const buf_t* content)
{
    put(b, id, 4);
    put_i32(b, (int32_t)content->n);
    put_i32(b, 0);
    put(b, content->p, content->n);
}

/* "k=v;k=v" -> MATL chunk */
static void put_matl(buf_t* out, int32_t id, const char* kv)
{
    buf_t       c     = { NULL, 0, 0 };
    const char* s     = kv;
    int32_t     count = 1;
    while (*s)
        if (*s++ == ';')
            ++count;

    put_i32(&c, id);
    put_i32(&c, count);
    s = kv;
    while (*s)
    {
        const char* eq  = strchr(s, '=');
        const char* end = strchr(s, ';');
        if (!end)
            end = s + strlen(s);

        put_str(&c, s, (size_t)(eq - s));
        put_str(&c, eq + 1, (size_t)(end - eq - 1));
        s = *end ? end + 1 : end;
    }
    put_chunk(out, "MATL", &c);
    free(c.p);
}

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-5f)

static void test_materials(void)
{
    buf_t            file = { NULL, 0, 0 }, main_c = { NULL, 0, 0 }, size_c = { NULL, 0, 0 }, xyzi = { NULL, 0, 0 };
    v4c_load_options opt;
    v4c_scene*       s = NULL;
    int32_t          i;
    int              pass;

    put_i32(&size_c, 46);
    put_i32(&size_c, 1);
    put_i32(&size_c, 1);
    put_i32(&xyzi, 46);
    for (i = 0; i < 46; ++i)
    {
        unsigned char v[4] = { (unsigned char)i, 0, 0, (unsigned char)(i + 1) };
        put(&xyzi, v, 4);
    }

    put_chunk(&main_c, "SIZE", &size_c);
    put_chunk(&main_c, "XYZI", &xyzi);
    for (i = 0; i < 46; ++i)
        put_matl(&main_c, i + 1, k_matl_dump[i]);

    put(&file, "VOX ", 4);
    put_i32(&file, 200);
    put(&file, "MAIN", 4);
    put_i32(&file, 0);
    put_i32(&file, (int32_t)main_c.n);
    put(&file, main_c.p, main_c.n);

    for (pass = 0; pass < 2; ++pass)
    {
        v4c_default_load_options(&opt);
        if (pass)
            opt.flags |= V4C_FLAG_USED_MATERIALS_ONLY;

        CHECK(v4c_load_memory(file.p, file.n, &opt, &s) == V4C_SUCCESS, "material file");
        if (!s)
            continue;

        CHECK(s->num_materials == (pass ? 46u : 256u), "material count %u", s->num_materials);
        {
/* material for palette index p */
#define M(p) (&s->materials[pass ? (p) - 1 : (p)])
            CHECK(M(1)->palette_index == 1 && M(46)->palette_index == 46, "palette index");
            for (i = 1; i <= 8; ++i)
                CHECK(M(i)->type == V4C_MATERIAL_MEDIA, "media type %d", i);
            for (i = 9; i <= 16; ++i)
                CHECK(M(i)->type == V4C_MATERIAL_BLEND, "blend type %d", i);
            for (i = 17; i <= 24; ++i)
                CHECK(M(i)->type == V4C_MATERIAL_EMIT, "emit type %d", i);
            for (i = 25; i <= 32; ++i)
                CHECK(M(i)->type == V4C_MATERIAL_GLASS, "glass type %d", i);
            for (i = 33; i <= 40; ++i)
                CHECK(M(i)->type == V4C_MATERIAL_METAL, "metal type %d", i);
            for (i = 41; i <= 46; ++i)
                CHECK(M(i)->type == V4C_MATERIAL_DIFFUSE, "diffuse type %d", i);

            /* defaults */
            CHECK(NEAR(M(1)->ior, 1.3f) && NEAR(M(1)->roughness, 0.1f) && NEAR(M(1)->density, 0.05f) &&
                      NEAR(M(1)->phase, 0.0f) && M(1)->media_type == V4C_MEDIA_ABSORB && NEAR(M(1)->specular, 1.0f) &&
                      NEAR(M(1)->metallic, 0.0f) && NEAR(M(1)->transparency, 0.0f),
                  "default values");
            if (!pass)
                CHECK(NEAR(s->materials[200].ior, 1.3f) && s->materials[200].type == V4C_MATERIAL_DIFFUSE,
                      "defaults for a material without MATL");

            /* media */
            CHECK(M(2)->media_type == V4C_MEDIA_SCATTER && M(3)->media_type == V4C_MEDIA_EMIT &&
                      M(4)->media_type == V4C_MEDIA_SSS && M(8)->media_type == V4C_MEDIA_ABSORB,
                  "media types");
            CHECK(NEAR(M(5)->phase, 0.03f) && NEAR(M(5)->density, 0.049f), "media g/d 5");
            CHECK(NEAR(M(7)->phase, 0.9f) && NEAR(M(7)->density, 0.055f), "media g/d 7");
            /* blend */
            CHECK(M(10)->media_type == V4C_MEDIA_SCATTER && NEAR(M(10)->transparency, 0.01f) &&
                      NEAR(M(10)->metallic, 0.01f) && NEAR(M(10)->roughness, 0.11f) && NEAR(M(10)->ior, 1.31f) &&
                      NEAR(M(10)->specular, 1.1f) && NEAR(M(10)->phase, 0.01f) && NEAR(M(10)->density, 0.051f),
                  "blend 10");
            CHECK(M(12)->media_type == V4C_MEDIA_SSS && NEAR(M(12)->specular, 1.01f), "blend 12");
            /* emit */
            CHECK(NEAR(M(18)->emissive, 0.01f) && NEAR(M(18)->emission_power, 2.0f) && NEAR(M(18)->ldr, 0.01f),
                  "emit 18");
            CHECK(NEAR(M(19)->ldr, 1.0f) && NEAR(M(20)->emission_power, 4.0f) && NEAR(M(21)->emissive, 1.0f),
                  "emit 19-21");
            CHECK(NEAR(M(17)->emissive, 0.0f) && NEAR(M(17)->emission_power, 0.0f), "emit defaults");
            /* glass */
            CHECK(M(28)->media_type == V4C_MEDIA_SSS && NEAR(M(29)->transparency, 0.01f) && NEAR(M(29)->ior, 1.31f),
                  "glass");
            /* metal */
            CHECK(NEAR(M(34)->metallic, 0.01f) && NEAR(M(34)->specular, 1.01f), "metal 34");
            CHECK(NEAR(M(35)->roughness, 1.0f) && NEAR(M(36)->ior, 3.0f) && NEAR(M(37)->specular, 2.0f), "metal 35-37");
            CHECK(NEAR(M(39)->metallic, 1.0f) && NEAR(M(39)->roughness, 1.0f) && NEAR(M(39)->ior, 3.0f) &&
                      NEAR(M(39)->specular, 2.0f),
                  "metal 39");
#undef M
        }
        v4c_free_scene(s);
        s = NULL;
    }

    /* _ri alone and _media alone (without the string keys) */
    {
        buf_t f2 = { NULL, 0, 0 }, m2 = { NULL, 0, 0 };
        put(&m2, main_c.p, main_c.n);
        put_matl(&m2, 5, "_type=_glass;_ri=1.7;_media=2"); /* later MATL for the same id wins */
        put(&f2, "VOX ", 4);
        put_i32(&f2, 200);
        put(&f2, "MAIN", 4);
        put_i32(&f2, 0);
        put_i32(&f2, (int32_t)m2.n);
        put(&f2, m2.p, m2.n);
        CHECK(v4c_load_memory(f2.p, f2.n, NULL, &s) == V4C_SUCCESS && s, "ri file");
        if (s)
            CHECK(s->materials[5].type == V4C_MATERIAL_GLASS && NEAR(s->materials[5].ior, 1.7f) &&
                      s->materials[5].media_type == V4C_MEDIA_EMIT,
                  "_ri / _media fallback");

        v4c_free_scene(s);
        s = NULL;
        free(f2.p);
        free(m2.p);
    }
    free(file.p);
    free(main_c.p);
    free(size_c.p);
    free(xyzi.p);
}

/* ---- render settings (rOBJ) and cameras (rCAM) ---- */

/* dictionary "k=v;k=v" (optionally preceded by an int32 id) as a chunk */
static void put_dict_chunk(buf_t* out, const char* chunk_id, int with_id, int32_t id, const char* kv)
{
    buf_t       c     = { NULL, 0, 0 };
    const char* p     = kv;
    int32_t     count = 1;
    while (*p)
        if (*p++ == ';')
            ++count;

    if (with_id)
        put_i32(&c, id);

    put_i32(&c, count);
    p = kv;
    while (*p)
    {
        const char* eq  = strchr(p, '=');
        const char* end = strchr(p, ';');
        if (!end)
            end = p + strlen(p);

        put_str(&c, p, (size_t)(eq - p));
        put_str(&c, eq + 1, (size_t)(end - eq - 1));
        p = *end ? end + 1 : end;
    }
    put_chunk(out, chunk_id, &c);
    free(c.p);
}

/* a 40x40x40 cube lifted by 20 (like the user's file) + the given chunks */
static unsigned char* make_render_file(const char* const* robj, int num_robj, const char* const* rcam, int num_rcam,
                                       size_t* size)
{
    buf_t         file = { NULL, 0, 0 }, main_c = { NULL, 0, 0 }, size_c = { NULL, 0, 0 }, xyzi = { NULL, 0, 0 };
    buf_t         trn0 = { NULL, 0, 0 }, grp = { NULL, 0, 0 }, trn2 = { NULL, 0, 0 }, shp = { NULL, 0, 0 };
    unsigned char v[4] = { 0, 0, 0, 1 }, w[4] = { 39, 39, 39, 1 };
    int           i;
    put_i32(&size_c, 40);
    put_i32(&size_c, 40);
    put_i32(&size_c, 40);
    put_i32(&xyzi, 2);
    put(&xyzi, v, 4);
    put(&xyzi, w, 4);
    /* nTRN 0 -> nGRP 1 -> nTRN 2 (t = 0 0 20) -> nSHP 3 */
    put_i32(&trn0, 0);
    put_i32(&trn0, 0);
    put_i32(&trn0, 1);
    put_i32(&trn0, -1);
    put_i32(&trn0, -1);
    put_i32(&trn0, 1);
    put_i32(&trn0, 0);
    put_i32(&grp, 1);
    put_i32(&grp, 0);
    put_i32(&grp, 1);
    put_i32(&grp, 2);
    put_i32(&trn2, 2);
    put_i32(&trn2, 0);
    put_i32(&trn2, 3);
    put_i32(&trn2, -1);
    put_i32(&trn2, 0);
    put_i32(&trn2, 1);
    put_i32(&trn2, 1);
    put_str(&trn2, "_t", 2);
    put_str(&trn2, "0 0 20", 6);
    put_i32(&shp, 3);
    put_i32(&shp, 0);
    put_i32(&shp, 1);
    put_i32(&shp, 0);
    put_i32(&shp, 0);
    put_chunk(&main_c, "SIZE", &size_c);
    put_chunk(&main_c, "XYZI", &xyzi);
    put_chunk(&main_c, "nTRN", &trn0);
    put_chunk(&main_c, "nGRP", &grp);
    put_chunk(&main_c, "nTRN", &trn2);
    put_chunk(&main_c, "nSHP", &shp);
    for (i = 0; i < num_robj; ++i)
        put_dict_chunk(&main_c, "rOBJ", 0, 0, robj[i]);

    for (i = num_rcam - 1; i >= 0; --i) /* written in reverse: the loader must sort them */
        put_dict_chunk(&main_c, "rCAM", 1, (int32_t)i, rcam[i]);

    put(&file, "VOX ", 4);
    put_i32(&file, 200);
    put(&file, "MAIN", 4);
    put_i32(&file, 0);
    put_i32(&file, (int32_t)main_c.n);
    put(&file, main_c.p, main_c.n);
    free(main_c.p);
    free(size_c.p);
    free(xyzi.p);
    free(trn0.p);
    free(grp.p);
    free(trn2.p);
    free(shp.p);
    *size = file.n;
    return file.p;
}

static float dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* irradiance on the axis of a Lambertian disk of radiance L, radius r, distance d,
   by numerical integration over rings: E = int L cos^2(t) / s^2 dA */
static double disk_irradiance(double L, double r, double d)
{
    double e = 0.0;
    int    i, n = 20000;
    for (i = 0; i < n; ++i)
    {
        const double rho = (i + 0.5) * r / n, s2 = rho * rho + d * d;
        e += L * (d * d / s2) / s2 * 2.0 * 3.14159265358979323846 * rho * (r / n);
    }
    return e;
}

/* NOLINTNEXTLINE(readability-function-size): a test case, long by nature */
static void test_render_settings(void)
{
    /* the rOBJ / rCAM content of the user's sun_and_background.vox */
    static const char* robj[] = {
        "_type=_bounce;_diffuse=2;_specular=5;_scatter=5;_energy=3",
        "_type=_env;_mode=0",
        "_type=_inf;_i=0.533005;_k=255 255 255;_angle=70 108;_area=0.543278;_disk=0",
        "_type=_uni;_i=0.590049;_k=255 255 255",
        "_type=_ibl;_path=HDR_041_Path_Env.hdr;_i=1;_angle=0 0",
        "_type=_lens;_proj=2;_fov=50;_aperture=0.416007;_blade_n=1;_blade_r=-66.8799",
        "_type=_bg;_color=255 255 255",
    };
    /* the camera slots of the user's test file (40^3 cube, centre at 0 0 20); slots 0, 1
       and 2 have MagicaVoxel screenshots: front, turned right by 90, from above at 45 */
    static const char* rcam[] = {
        "_mode=pers;_focus=0 0 20;_angle=0 0 0;_radius=89.282;_frustum=0.414214;_fov=45",
        "_mode=pers;_focus=0 0 20;_angle=0 90 0;_radius=89.282;_frustum=0.414214;_fov=45",
        "_mode=pers;_focus=0 0 20;_angle=-45 0 0;_radius=89.282;_frustum=0.414214;_fov=45",
        "_mode=pers;_focus=-7.89098 -1.91559 32.9644;_angle=-16.51 306.438 0;_radius=109.271;_frustum=0.612801;_fov=63",
        "_mode=free;_focus=18.9628 31.5299 -14.413;_angle=-34.7436 342.146 0;_radius=109.271;_frustum=0.612801;_fov=63",
        "_mode=orth;_focus=10.9865 40.6172 -5.46739;_angle=-34.7436 342.146 0;_radius=3248.8;_frustum=39.5399;_fov=63",
        "_mode=iso;_focus=-5.09823 9.58368 10.4484;_angle=-35.2644 22.0324 34;_radius=3287.09;_frustum=39.5399;_fov=63",
        "_mode=iso;_focus=-5.09823 9.58368 10.4484;_angle=-35.2644 22.0324 34;_radius=3287.09;_frustum=39.5399;_fov=33",
        "_mode=pers;_focus=0 0 22.1523;_angle=0 0 0;_radius=110;_frustum=0.296213;_fov=33",
        "_mode=orth;_focus=0 0 22.1523;_angle=0 0 0;_radius=2002;_frustum=32.5835;_fov=33",
        "_mode=pers;_focus=0 0 0;_angle=0 0 0;_radius=0;_frustum=0.414214;_fov=45",
    };
    static const char* directional[] = { "_type=_inf;_i=1;_k=255 200 100;_angle=90 0;_area=0;_disk=1",
                                         "_type=_len;_fov=30;_dof=0.3" };
    size_t             size;
    unsigned char*     file = make_render_file(robj, 7, rcam, 11, &size);
    unsigned char*     file2;
    int                cs;

    for (cs = 0; cs < 4; ++cs)
    {
        v4c_load_options opt;
        v4c_scene*       s = NULL;
        v4c_default_load_options(&opt);
        opt.coord_system = (v4c_coord_system)cs;
        opt.scale        = cs == 1 ? 0.5f : 1.0f;
        CHECK(v4c_load_memory(file, size, &opt, &s) == V4C_SUCCESS && s, "render file");
        if (!s)
            continue;
        {
            const v4c_render_settings* r   = &s->render;
            const v4c_sun*             sun = &r->sun;
            float                      mv_light[3], expect[3], mn[3], mx[3], target[3], len;
            int                        k;

            CHECK(r->has_background && r->background.r == 255 && r->background.g == 255 && r->background.b == 255,
                  "background");
            CHECK(r->has_ambient && NEAR(r->ambient_intensity, 0.590049f) && r->ambient_color.r == 255, "ambient");
            CHECK(r->has_lens && NEAR(r->fov, 50.0f) && r->projection == V4C_PROJECTION_PANORAMA &&
                      r->projection_index == 2 && NEAR(r->aperture, 0.416007f),
                  "lens");
            CHECK(sun->present && sun->type == V4C_LIGHT_AREA && NEAR(sun->intensity, 0.533005f) &&
                      NEAR(sun->elevation, 70.0f) && NEAR(sun->azimuth, 108.0f) && NEAR(sun->area, 0.543278f) &&
                      !sun->disk && sun->color.g == 255,
                  "sun values");

            /* direction: MagicaVoxel az 108 = right and a bit behind, el 70 = high */
            mv_light[0] = -cosf(70.0f * 0.0174532925f) * sinf(108.0f * 0.0174532925f);
            mv_light[1] = cosf(70.0f * 0.0174532925f) * cosf(108.0f * 0.0174532925f);
            mv_light[2] = -sinf(70.0f * 0.0174532925f);
            for (k = 0; k < 3; ++k)
            {
                int a     = (int)s->axes[k];
                expect[k] = (a & 1) ? -mv_light[a >> 1] : mv_light[a >> 1];
            }

            len = sqrtf(dot3(sun->direction, sun->direction));
            CHECK(fabsf(len - 1.0f) < 1e-5f && fabsf(dot3(sun->direction, expect) - 1.0f) < 1e-5f, "sun direction");
            /* "down" in the output system: the light must go down */
            {
                float down[3] = { 0.0f, 0.0f, 0.0f };
                if (cs <= 1)
                    down[2] = -1.0f;
                else
                    down[1] = -1.0f;
                CHECK(dot3(sun->direction, down) > 0.9f, "sun light points down (cs %d)", cs);
            }

            /* default area light: on the axis through the scene centre, far away, same irradiance */
            CHECK(v4c_scene_bounds(s, 0, false, mn, mx), "bounds");
            for (k = 0; k < 3; ++k)
                target[k] = 0.5f * (mn[k] + mx[k]);
            {
                const v4c_area_light* a = &sun->area_light;
                float                 back[3];
                double                e;
                for (k = 0; k < 3; ++k)
                    back[k] = (target[k] - a->position[k]) / a->distance;

                CHECK(fabsf(dot3(back, sun->direction) - 1.0f) < 1e-4f, "area light is on the sun axis");
                CHECK(a->distance > 50.0f * 20.0f * opt.scale, "area light far away (%g)", a->distance);
                e = disk_irradiance(a->radiance, a->radius, a->distance);
                CHECK(fabs(e - 0.533005) < 1e-3, "area light irradiance %g == directional 0.533005", e);
                CHECK(fabsf(a->power - 3.14159265f * a->radiance * 3.14159265f * a->radius * a->radius) <
                          1e-3f * a->power,
                      "power == pi * L * area");
                CHECK(NEAR(a->intensity * 3.14159265f / a->power, 1.0f), "intensity == power / pi");
            }

            /* bounds of the cube: 40^3 lifted by 20, pivot at its centre bottom... in MV units */
            CHECK(fabsf((mx[0] - mn[0]) - 40.0f * opt.scale) < 1e-3f &&
                      fabsf((mx[1] - mn[1]) - 40.0f * opt.scale) < 1e-3f &&
                      fabsf((mx[2] - mn[2]) - 40.0f * opt.scale) < 1e-3f,
                  "bounds size");

            /* cameras: sorted by id, empty slots recognised, values converted */
            CHECK(s->num_cameras == 11, "camera count");
            for (k = 0; k < (int)s->num_cameras; ++k)
                CHECK(s->cameras[k].id == (uint32_t)k, "camera order");

            if (s->num_cameras == 11)
            {
                /* expected poses in MagicaVoxel coordinates (voxels), from the screenshots:
                   0 front: camera in front (-Y) looking +Y; 1 right: camera at +X looking -X;
                   2 from above at 45 degrees: looking down, sees top and front */
                static const float mv_pos[3][3] = { { 0.0f, -89.282f, 20.0f },
                                                    { 89.282f, 0.0f, 20.0f },
                                                    { 0.0f, -63.1319f, 83.1319f } };
                static const float mv_fwd[3][3] = { { 0.0f, 1.0f, 0.0f },
                                                    { -1.0f, 0.0f, 0.0f },
                                                    { 0.0f, 0.70710678f, -0.70710678f } };
                int                slot;
                for (slot = 0; slot < 3; ++slot)
                {
                    float pos[3], fwd[3], up[3], ep[3], ef[3], eu[3], mv_up[3];
                    /* up = forward rotated towards +Z: for these slots (0 0 1) or (0 .707 .707) */
                    mv_up[0] = 0.0f;
                    mv_up[1] = slot == 2 ? 0.70710678f : 0.0f;
                    mv_up[2] = slot == 2 ? 0.70710678f : 1.0f;
                    for (k = 0; k < 3; ++k)
                    {
                        int a = (int)s->axes[k];
                        ep[k] = ((a & 1) ? -mv_pos[slot][a >> 1] : mv_pos[slot][a >> 1]) * opt.scale;
                        ef[k] = (a & 1) ? -mv_fwd[slot][a >> 1] : mv_fwd[slot][a >> 1];
                        eu[k] = (a & 1) ? -mv_up[a >> 1] : mv_up[a >> 1];
                    }

                    v4c_camera_pose(s, &s->cameras[slot], pos, fwd, up);
                    for (k = 0; k < 3; ++k)
                        CHECK(fabsf(pos[k] - ep[k]) < 1e-3f && fabsf(fwd[k] - ef[k]) < 1e-5f &&
                                  fabsf(up[k] - eu[k]) < 1e-5f,
                              "slot %d pose (cs %d) axis %d: pos %g/%g fwd %g/%g up %g/%g", slot, cs, k, pos[k], ep[k],
                              fwd[k], ef[k], up[k], eu[k]);
                    CHECK(s->cameras[slot].saved && s->cameras[slot].mode == V4C_CAMERA_PERSPECTIVE, "slot %d", slot);
                }
                /* front screenshot: the 40 voxel face fills 998 of 1024 pixels, which is what a
                   33 degree field of view gives from this distance (45 would give ~70%) */
                {
                    float pos[3], dist, fill;
                    v4c_camera_pose(s, &s->cameras[0], pos, NULL, NULL);
                    dist = (s->cameras[0].radius - 20.0f * opt.scale);
                    fill = 40.0f * opt.scale / (2.0f * dist * tanf(16.5f * 0.0174532925f));
                    CHECK(fabsf(fill - 998.0f / 1024.0f) < 0.002f, "front framing %g", fill);
                }
                /* isometric: MagicaVoxel's iso pitch (-35.26 = looking down), third angle ignored */
                {
                    float fwd[3], up[3], upw[3] = { 0.0f, 0.0f, 0.0f };
                    if (cs <= 1)
                        upw[2] = 1.0f;
                    else
                        upw[1] = 1.0f;
                    v4c_camera_pose(s, &s->cameras[6], NULL, fwd, up);
                    CHECK(s->cameras[6].mode == V4C_CAMERA_ISOMETRIC && NEAR(dot3(fwd, upw), -0.57735f), "iso pitch");
                    CHECK(fabsf(dot3(up, upw) - 0.81650f) < 1e-4f, "iso not rolled");
                }
                /* orthographic frustum = half the view height at the focus: slot 9 is slot 8
                   switched to orthographic and keeps the framing (110 * tan(16.5) = 32.58) */
                CHECK(s->cameras[9].mode == V4C_CAMERA_ORTHOGRAPHIC &&
                          fabsf(s->cameras[9].frustum - 32.5835f * opt.scale) < 1e-3f &&
                          fabsf(s->cameras[8].radius * s->cameras[8].frustum - s->cameras[9].frustum) < 1e-2f,
                      "ortho frustum");
                CHECK(s->cameras[4].mode == V4C_CAMERA_FREE && s->cameras[5].mode == V4C_CAMERA_ORTHOGRAPHIC &&
                          NEAR(s->cameras[3].fov, 63.0f) && NEAR(s->cameras[3].frustum, 0.612801f),
                      "other slots");
                CHECK(!s->cameras[10].saved, "untouched slot");
                /* every saved slot: orthonormal basis, looking at its focus */
                for (k = 0; k < 10; ++k)
                {
                    float pos[3], fwd[3], up[3];
                    int   a;
                    v4c_camera_pose(s, &s->cameras[k], pos, fwd, up);
                    CHECK(fabsf(dot3(fwd, fwd) - 1.0f) < 1e-5f && fabsf(dot3(up, up) - 1.0f) < 1e-5f &&
                              fabsf(dot3(fwd, up)) < 1e-5f,
                          "slot %d basis", k);

                    for (a = 0; a < 3; ++a)
                        CHECK(fabsf(pos[a] + fwd[a] * s->cameras[k].radius - s->cameras[k].focus[a]) <
                                  1e-3f * (1.0f + s->cameras[k].radius),
                              "slot %d looks at its focus", k);
                }
            }
        }
        v4c_free_scene(s);
    }
    free(file);

    /* _area = 0 -> directional; old _len chunk; no cameras */
    file2 = make_render_file(directional, 2, NULL, 0, &size);
    {
        v4c_scene* s = NULL;
        CHECK(v4c_load_memory(file2, size, NULL, &s) == V4C_SUCCESS && s, "directional file");
        if (s)
        {
            float          t[3] = { 0.0f, 0.0f, 0.0f };
            v4c_area_light al;
            const v4c_sun* sun = &s->render.sun;
            CHECK(sun->present && sun->type == V4C_LIGHT_DIRECTIONAL && sun->disk && sun->color.g == 200 &&
                      sun->color.b == 100,
                  "directional sun");
            CHECK(NEAR(sun->direction[0], 0.0f) && NEAR(sun->direction[1], 0.0f) && NEAR(sun->direction[2], -1.0f),
                  "elevation 90 = straight down");
            CHECK(!v4c_sun_area_light(s, t, 100.0f, &al), "no area light for area 0");
            CHECK(s->render.has_lens && NEAR(s->render.fov, 30.0f) && NEAR(s->render.aperture, 0.3f), "_len");
            CHECK(!s->render.has_background && !s->render.has_ambient && s->num_cameras == 0 && !s->cameras, "absent");
        }
        v4c_free_scene(s);
    }
    free(file2);
}

/* light_test.vox: free camera in slot 0 and a low sun from the right, with a
   MagicaVoxel render (light.png, 1024x1024, fov 45). The cube corners and the
   shadow direction are compared with that image. */
static void test_light_file(void)
{
    static const char* robj[] = {
        "_type=_inf;_i=0.7;_k=255 255 255;_angle=15 90;_area=0.197209;_disk=0",
        "_type=_uni;_i=0.7;_k=255 255 255",
        "_type=_lens;_proj=0;_fov=45;_aperture=0.25;_blade_n=0;_blade_r=0",
        "_type=_bg;_color=0 0 0",
    };
    static const char* rcam[] = {
        "_mode=free;_focus=-74.7072 -20.0212 38.1687;_angle=0 0 0;_radius=206.379;_frustum=0.414214;_fov=45",
        "_mode=pers;_focus=0 0 0;_angle=0 0 0;_radius=0;_frustum=0.414214;_fov=45",
    };
    size_t         size;
    unsigned char* file = make_render_file(robj, 4, rcam, 2, &size);
    v4c_scene*     s    = NULL;
    CHECK(v4c_load_memory(file, size, NULL, &s) == V4C_SUCCESS && s, "light file");
    if (s)
    {
        const v4c_sun*    sun = &s->render.sun;
        const v4c_camera* cam = &s->cameras[0];
        float             pos[3], fwd[3], up[3];
        /* azimuth 90 = sun on the right (+X), elevation 15: light travels to -X and down;
           in the render the long shadow runs to the left, parallel to X */
        CHECK(sun->type == V4C_LIGHT_AREA && NEAR(sun->intensity, 0.7f), "light sun");
        CHECK(fabsf(sun->direction[0] + 0.96592583f) < 1e-5f && fabsf(sun->direction[1]) < 1e-6f &&
                  fabsf(sun->direction[2] + 0.25881905f) < 1e-5f,
              "light sun direction");
        /* area 0.197209 -> 8.58 degrees; the penumbra in the render fits 8.6-10 */
        CHECK(fabsf(sun->angular_diameter - 8.5786f) < 1e-3f, "sun angular diameter");

        /* free mode camera: orbit around focus like the others (camera at focus - forward * radius) */
        CHECK(cam->mode == V4C_CAMERA_FREE && cam->saved, "free slot");
        v4c_camera_pose(s, cam, pos, fwd, up);
        CHECK(fabsf(pos[0] + 74.7072f) < 1e-3f && fabsf(pos[1] + 226.3999f) < 1e-3f && fabsf(pos[2] - 38.1687f) < 1e-3f,
              "free camera position");
        /* project cube corners with the render's fov and compare with the image */
        {
            static const float corners[3][3] = { { -20.0f, 20.0f, 40.0f },   /* top left of the -X face, far  */
                                                 { -20.0f, -20.0f, 0.0f },   /* bottom left of the front face */
                                                 { 20.0f, -20.0f, 40.0f } }; /* top of the front face        */
            static const float image[3][2]   = { { 787.0f, 501.0f }, { 838.0f, 740.0f }, { -1.0f, 501.0f } };
            const float        focal         = 512.0f / tanf(0.5f * s->render.fov * 0.0174532925f);
            float              right[3];
            int                i, k;
            right[0] = fwd[1] * up[2] - fwd[2] * up[1];
            right[1] = fwd[2] * up[0] - fwd[0] * up[2];
            right[2] = fwd[0] * up[1] - fwd[1] * up[0];
            for (i = 0; i < 3; ++i)
            {
                float d[3], x, y, z;
                for (k = 0; k < 3; ++k)
                    d[k] = corners[i][k] - pos[k];

                z = dot3(d, fwd);
                x = 512.0f + focal * dot3(d, right) / z;
                y = 512.0f - focal * dot3(d, up) / z;
                if (image[i][0] >= 0.0f)
                    CHECK(fabsf(x - image[i][0]) < 3.0f, "corner %d x %g vs %g", i, x, image[i][0]);
                CHECK(fabsf(y - image[i][1]) < 3.0f, "corner %d y %g vs %g", i, y, image[i][1]);
            }
        }
    }
    v4c_free_scene(s);
    free(file);
}

/* Lenses: lens_test.vox rendered by MagicaVoxel at 2048x1024 with every lens
   at fov 45 and 90. Camera slot 0 at (0 0 24) looking +Y. The table holds
   cube corners (MagicaVoxel coordinates) and where their edges are in the
   renders (measured from the images, edges slightly blurred by the depth of
   field). */
typedef struct lens_case
{
    int   projection;
    float fov;
    float point[3];
    int   axis; /* 0: x, 1: y */
    float measured;
    int   width; /* 0: 2048 x 1024 */
    int   height;
} lens_case;

/* NOLINTNEXTLINE(readability-function-size): a test case, long by nature */
static void test_lenses(void)
{
    static const lens_case cases[] = {
        { 0, 45.0f, { 0.0f, 74.0f, 48.0f }, 1, 113.0f, 0, 0 },   /* dark green top edge   */
        { 0, 45.0f, { 24.0f, 78.0f, 24.0f }, 0, 1405.0f, 0, 0 }, /* dark red left edge    */
        { 0, 45.0f, { 30.0f, 72.0f, 24.0f }, 0, 1539.0f, 0, 0 }, /* dark red right edge   */
        { 0, 90.0f, { 54.0f, 60.0f, 24.0f }, 0, 1484.0f, 0, 0 }, /* orange left edge      */
        { 0, 90.0f, { 60.0f, 54.0f, 24.0f }, 0, 1595.0f, 0, 0 }, /* orange right edge     */
        { 0, 90.0f, { 0.0f, 74.0f, 48.0f }, 1, 347.0f, 0, 0 },
        { 0, 90.0f, { 24.0f, 78.0f, 24.0f }, 0, 1182.0f, 0, 0 },
        { 1, 45.0f, { 0.0f, 74.0f, 48.0f }, 1, 106.0f, 0, 0 },
        { 1, 45.0f, { 24.0f, 78.0f, 24.0f }, 0, 1411.0f, 0, 0 },
        { 1, 45.0f, { 30.0f, 72.0f, 24.0f }, 0, 1539.0f, 0, 0 },
        { 1, 90.0f, { 0.0f, 74.0f, 48.0f }, 1, 317.0f, 0, 0 },
        { 1, 90.0f, { 24.0f, 78.0f, 24.0f }, 0, 1210.0f, 0, 0 },
        { 1, 90.0f, { -54.0f, 54.0f, 24.0f }, 0, 511.5f, 0, 0 }, /* pink cube centre     */
        { 2, 45.0f, { 24.0f, 78.0f, 24.0f }, 0, 1121.0f, 0, 0 },
        { 2, 45.0f, { 30.0f, 72.0f, 24.0f }, 0, 1153.0f, 0, 0 },
        { 2, 90.0f, { -80.0f, 0.0f, 24.0f }, 0, 512.0f, 0, 0 }, /* purple, 90 left      */
        { 2, 90.0f, { 80.0f, 0.0f, 24.0f }, 0, 1536.0f, 0, 0 }, /* yellow, 90 right     */
        { 2, 90.0f, { 0.0f, 40.0f, 93.0f }, 1, 172.0f, 0, 0 },  /* brown, 60 up (centre) */
        /* square 1024 x 1024 panorama (fov 45): 180 degrees across, not stretched */
        { 2, 45.0f, { 24.0f, 78.0f, 24.0f }, 0, 609.0f, 1024, 1024 },  /* dark red left edge   */
        { 2, 45.0f, { 30.0f, 72.0f, 24.0f }, 0, 641.0f, 1024, 1024 },  /* dark red right edge  */
        { 2, 45.0f, { -57.0f, 57.0f, 24.0f }, 0, 256.5f, 1024, 1024 }, /* pink, 45 left        */
        { 2, 45.0f, { -77.0f, 3.0f, 24.0f }, 0, 13.0f, 1024, 1024 },   /* purple right edge    */
        { 2, 45.0f, { 77.0f, 3.0f, 24.0f }, 0, 1010.0f, 1024, 1024 },  /* yellow left edge     */
        { 2, 45.0f, { 0.0f, 40.0f, 93.0f }, 1, 172.0f, 1024, 1024 },   /* brown, 60 up         */
        /* 3072 x 1024 (540 degrees: directions repeat every 2048 px) and 2050 x 1024 (odd size) */
        { 2, 45.0f, { 24.0f, 78.0f, 24.0f }, 0, 1634.0f, 3072, 1024 },  /* dark red left edge   */
        { 2, 45.0f, { 30.0f, 72.0f, 24.0f }, 0, 1665.0f, 3072, 1024 },  /* dark red right edge  */
        { 2, 45.0f, { -57.0f, 57.0f, 24.0f }, 0, 1280.5f, 3072, 1024 }, /* pink, 45 left        */
        { 2, 45.0f, { -80.0f, 0.0f, 24.0f }, 0, 1024.5f, 3072, 1024 },  /* purple, 90 left      */
        { 2, 45.0f, { 24.0f, 78.0f, 24.0f }, 0, 1123.0f, 2050, 1024 },
        { 2, 45.0f, { 30.0f, 72.0f, 24.0f }, 0, 1154.0f, 2050, 1024 },
        { 2, 45.0f, { -57.0f, 57.0f, 24.0f }, 0, 769.5f, 2050, 1024 },
        { 2, 45.0f, { -80.0f, 0.0f, 24.0f }, 0, 513.5f, 2050, 1024 },
    };
    static const char* rcam[] = {
        "_mode=pers;_focus=0 0 24;_angle=0 0 0;_radius=0.001;_frustum=0.414214;_fov=45",
    };
    int cs, i;
    for (cs = 0; cs < 4; ++cs)
    {
        for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); ++i)
        {
            const lens_case* lc = &cases[i];
            char             lens[128];
            const char*      robj[1];
            size_t           size;
            unsigned char*   file;
            v4c_load_options opt;
            v4c_scene*       s = NULL;
            snprintf(lens, sizeof(lens), "_type=_lens;_proj=%d;_fov=%g;_aperture=0.25", lc->projection,
                     (double)lc->fov);
            robj[0] = lens;
            file    = make_render_file(robj, 1, rcam, 1, &size);
            v4c_default_load_options(&opt);
            opt.coord_system = (v4c_coord_system)cs;
            opt.scale        = cs == 2 ? 0.1f : 1.0f;
            CHECK(v4c_load_memory(file, size, &opt, &s) == V4C_SUCCESS && s, "lens file");
            if (s)
            {
                float pt[3], px[2] = { 0.0f, 0.0f }, o[3], d[3];
                int   k;
                CHECK((int)s->render.projection == lc->projection && NEAR(s->render.fov, lc->fov), "lens parsed");
                for (k = 0; k < 3; ++k)
                {
                    const int a = (int)s->axes[k];
                    pt[k]       = ((a & 1) ? -lc->point[a >> 1] : lc->point[a >> 1]) * opt.scale;
                }

                const uint32_t w = lc->width ? (uint32_t)lc->width : 2048u,
                               h = lc->height ? (uint32_t)lc->height : 1024u;
                if (!v4c_camera_project(s, &s->cameras[0], s->render.projection, s->render.fov, w, h, pt, px))
                {
                    CHECK(0, "project (case %d cs %d)", i, cs);
                    v4c_free_scene(s);
                    free(file);
                    continue;
                }
                CHECK(fabsf(px[lc->axis] - lc->measured) < 2.5f, "lens %d fov %g case %d (cs %d): %g vs measured %g",
                      lc->projection, (double)lc->fov, i, cs, (double)px[lc->axis], (double)lc->measured);

                /* the ray of that pixel must point at the point */
                if (!v4c_camera_ray(s, &s->cameras[0], s->render.projection, s->render.fov, w, h, px[0], px[1], o, d))
                {
                    CHECK(0, "ray (case %d cs %d)", i, cs);
                }
                else
                {
                    float v[3], len;
                    for (k = 0; k < 3; ++k)
                        v[k] = pt[k] - o[k];

                    len = sqrtf(dot3(v, v));
                    CHECK(dot3(v, d) / len > 0.99999f, "ray points at the point (case %d cs %d)", i, cs);
                }
            }
            v4c_free_scene(s);
            free(file);
        }
    }

    /* 3072 x 1024 panorama: the back-left (-135) and left (-90) markers appear twice,
       2048 px apart (at 768 / 2816 and 1024 / 3072 in the render); both copies must
       look at the marker */
    {
        static const char* robj[]       = { "_type=_lens;_proj=2;_fov=45" };
        static const float copies[4][4] = { { 768.0f, -54.0f, -57.0f, 24.0f },
                                            { 2816.0f, -54.0f, -57.0f, 24.0f },
                                            { 1024.0f, -80.0f, 0.0f, 24.0f },
                                            { 3071.5f, -80.0f, 0.0f, 24.0f } };
        size_t             size;
        unsigned char*     file = make_render_file(robj, 1, rcam, 1, &size);
        v4c_scene*         s    = NULL;
        CHECK(v4c_load_memory(file, size, NULL, &s) == V4C_SUCCESS && s, "wide panorama file");
        if (s)
        {
            for (i = 0; i < 4; ++i)
            {
                float o[3], d[3], v[3];
                int   k;
                CHECK(v4c_camera_ray(s, &s->cameras[0], V4C_PROJECTION_PANORAMA, 45.0f, 3072, 1024, copies[i][0],
                                     512.0f, o, d),
                      "wide ray");
                for (k = 0; k < 3; ++k)
                    v[k] = copies[i][k + 1] - o[k];

                CHECK(dot3(v, d) / sqrtf(dot3(v, v)) > 0.9995f, "copy %d at x %g looks at its marker", i,
                      (double)copies[i][0]);
            }
        }
        v4c_free_scene(s);
        free(file);
    }

    /* round trips pixel -> ray -> pixel for every lens, camera mode and handedness */
    {
        static const char* cams[] = {
            "_mode=pers;_focus=3 -5 20;_angle=-20 35 0;_radius=60;_frustum=0.414214;_fov=45",
            "_mode=orth;_focus=3 -5 20;_angle=-20 35 0;_radius=2000;_frustum=30;_fov=45",
            "_mode=iso;_focus=0 0 10;_angle=-35.2644 45 0;_radius=2000;_frustum=25;_fov=45",
            "_mode=pano;_focus=0 0 10;_angle=10 200 0;_radius=5;_frustum=0.414214;_fov=45",
        };
        int proj;
        for (proj = 0; proj < 3; ++proj)
        {
            char           lens[96];
            const char*    robj[1];
            size_t         size;
            unsigned char* file;
            snprintf(lens, sizeof(lens), "_type=_lens;_proj=%d;_fov=70", proj);
            robj[0] = lens;
            file    = make_render_file(robj, 1, cams, 4, &size);
            for (cs = 0; cs < 4; ++cs)
            {
                v4c_load_options opt;
                v4c_scene*       s = NULL;
                v4c_default_load_options(&opt);
                opt.coord_system = (v4c_coord_system)cs;
                CHECK(v4c_load_memory(file, size, &opt, &s) == V4C_SUCCESS && s, "round trip file");
                if (s)
                {
                    int c, j;
                    for (c = 0; c < 4; ++c)
                    {
                        for (j = 0; j < 40; ++j)
                        {
                            const float px = 8.0f + (float)(j * 37 % 40) * 45.0f,
                                        py = 5.0f + (float)(j * 13 % 40) * 18.0f;
                            float       o[3], d[3], p[3], back[2];
                            int         k;
                            CHECK(
                                v4c_camera_ray(s, &s->cameras[c], s->render.projection, 70.0f, 1800, 720, px, py, o, d),
                                "rt ray");
                            for (k = 0; k < 3; ++k)
                                p[k] = o[k] + d[k] * 50.0f;

                            CHECK(
                                v4c_camera_project(s, &s->cameras[c], s->render.projection, 70.0f, 1800, 720, p, back),
                                "rt project");
                            /* a panorama wider than 2:1 repeats every 360 degrees = 2 * height pixels */
                            if ((c == 3 || proj == 2) && c != 1 && c != 2)
                            {
                                float dx = back[0] - px;
                                while (dx > 720.0f)
                                    dx -= 1440.0f;

                                while (dx < -720.0f)
                                    dx += 1440.0f;

                                back[0] = px + dx;
                            }
                            CHECK(fabsf(back[0] - px) < 0.02f && fabsf(back[1] - py) < 0.02f,
                                  "round trip lens %d cam %d cs %d: (%g %g) -> (%g %g)", proj, c, cs, (double)px,
                                  (double)py, (double)back[0], (double)back[1]);
                        }

                        /* image centre looks along the camera's forward vector, right half is on its right */
                        {
                            float o[3], d[3], pos[3], fwd[3], up[3], r[3], o2[3], d2[3];
                            int   k;
                            v4c_camera_pose(s, &s->cameras[c], pos, fwd, up);
                            v4c_camera_ray(s, &s->cameras[c], s->render.projection, 70.0f, 1800, 720, 900.0f, 360.0f, o,
                                           d);
                            CHECK(dot3(d, fwd) > 0.99999f, "centre ray = forward");
                            v4c_camera_ray(s, &s->cameras[c], s->render.projection, 70.0f, 1800, 720, 1200.0f, 360.0f,
                                           o2, d2);
                            /* right vector: the direction the second pixel moved to */
                            for (k = 0; k < 3; ++k)
                                r[k] = (o2[k] + d2[k]) - (o[k] + d[k]);

                            {
                                /* MagicaVoxel right of a camera looking +Y with +Z up is +X:
                                   in output axes that is axes-converted (1,0,0) for camera 0 at yaw 0 */
                                float cr[3];
                                cr[0] = fwd[1] * up[2] - fwd[2] * up[1];
                                cr[1] = fwd[2] * up[0] - fwd[0] * up[2];
                                cr[2] = fwd[0] * up[1] - fwd[1] * up[0];
                                if (s->left_handed)
                                    for (k = 0; k < 3; ++k)
                                        cr[k] = -cr[k];

                                CHECK(dot3(r, cr) > 0.0f, "right half of the image is on the right (cam %d cs %d)", c,
                                      cs);
                            }
                        }
                    }
                }
                v4c_free_scene(s);
            }
            free(file);
        }
    }
}

/* Files of MagicaVoxel 0.98 and older (version 150): no scene graph, PACK,
   no RGBA chunk when the default palette is used. */
static unsigned char* make_legacy_file(int frames, size_t* size)
{
    buf_t         file = { NULL, 0, 0 }, main_c = { NULL, 0, 0 }, pack = { NULL, 0, 0 };
    unsigned char v[4] = { 1, 2, 4, 9 };
    int           i;
    put_i32(&pack, frames);
    put_chunk(&main_c, "PACK", &pack);
    for (i = 0; i < frames; ++i)
    {
        buf_t size_c = { NULL, 0, 0 }, xyzi = { NULL, 0, 0 };
        put_i32(&size_c, 3);
        put_i32(&size_c, 4);
        put_i32(&size_c, 5 + i); /* only the first frame decides the placement */
        put_i32(&xyzi, 1);
        put(&xyzi, v, 4);
        put_chunk(&main_c, "SIZE", &size_c);
        put_chunk(&main_c, "XYZI", &xyzi);
        free(size_c.p);
        free(xyzi.p);
    }

    put(&file, "VOX ", 4);
    put_i32(&file, 150);
    put(&file, "MAIN", 4);
    put_i32(&file, 0);
    put_i32(&file, (int32_t)main_c.n);
    put(&file, main_c.p, main_c.n);
    free(main_c.p);
    free(pack.p);
    *size = file.n;
    return file.p;
}

static void test_legacy_files(void)
{
    int frames;
    printf("legacy (version 150) files\n");
    for (frames = 1; frames <= 2; ++frames)
    {
        size_t           size;
        unsigned char*   file = make_legacy_file(frames, &size);
        v4c_load_options opt;
        v4c_scene*       s = NULL;
        int              yup;
        for (yup = 0; yup <= 1; ++yup)
        {
            v4c_default_load_options(&opt);
            opt.coord_system = yup ? V4C_COORDS_Y_UP_RIGHT_HANDED : V4C_COORDS_Z_UP_RIGHT_HANDED;
            CHECK(v4c_load_memory(file, size, &opt, &s) == V4C_SUCCESS && s, "legacy file (%d frame(s)) loads", frames);
            if (!s)
                continue;

            CHECK(s->file_version == 150 && s->num_models == (uint32_t)frames, "legacy models");
            /* root + one model node, like MagicaVoxel's own conversion */
            CHECK(s->num_nodes == 2 && s->nodes[0].num_children == 1, "legacy graph: %u nodes", s->num_nodes);
            if (s->num_nodes == 2)
            {
                const v4c_node*      n = &s->nodes[1];
                const v4c_transform* t = &n->transform_keys[0].transform;
                /* stood on the ground: translation z = floor(5 / 2) = 2 */
                const float expect[3] = { 0.0f, yup ? 2.0f : 0.0f, yup ? 0.0f : 2.0f };
                CHECK(n->num_transform_keys == 1 && floats_equal(t->translation, expect, 3),
                      "legacy placement (%g, %g, %g)", t->translation[0], t->translation[1], t->translation[2]);
                CHECK(n->num_model_keys == (uint32_t)frames, "legacy model keys: %u", n->num_model_keys);
                if (frames == 2 && n->num_model_keys == 2)
                    CHECK(n->model_keys[1].frame == 1 && n->model_keys[1].model == 1, "PACK frame 1 -> model 1");
            }

            /* no RGBA chunk: MagicaVoxel's default palette, color 9 = #ffcc99 */
            if (s->num_models > 0 && s->models[0].num_voxels == 1)
            {
                const v4c_color c = s->materials[s->models[0].voxels[0].material].color;
                CHECK(c.r == 255 && c.g == 204 && c.b == 153 && c.a == 255, "default palette color 9: %d %d %d %d", c.r,
                      c.g, c.b, c.a);
            }
            v4c_free_scene(s);
            s = NULL;
        }
        free(file);
    }
}

/* MATT (0.98) and MATL dictionaries of 0.99.1 / 0.99.4, as those versions write
   them (taken from chr_knight saved by each version). The expected values are
   what MagicaVoxel 0.99.7 writes when it re-saves those files. */
static void put_matt(buf_t* out, int32_t id, int32_t type, float weight, uint32_t bits, const float* values, int n)
{
    buf_t c = { NULL, 0, 0 };
    int   i;
    put_i32(&c, id);
    put_i32(&c, type);
    put(&c, &weight, 4); /* little-endian hosts only, like the rest of the test data */
    put_i32(&c, (int32_t)bits);
    for (i = 0; i < n; ++i)
        put(&c, &values[i], 4);

    put_chunk(out, "MATT", &c);
    free(c.p);
}

static void test_legacy_materials(void)
{
    static const float v_glass[5] = { 0.0f, 0.378947f, 0.5f, 0.505263f, 0.326316f };
    static const float v_emit[6]  = { 0.0f, 0.1f, 0.5f, 0.3f, 2.0f, 0.231579f }; /* bit 7: no value */
    buf_t              file = { NULL, 0, 0 }, main_c = { NULL, 0, 0 };
    v4c_scene*         s = NULL;

    printf("legacy materials (0.98 MATT, 0.99.1 / 0.99.4 MATL)\n");
    /* 0.98.2 */
    put_matt(&main_c, 9, 2, 0.305263f, 0x1f, v_glass, 5);
    put_matt(&main_c, 3, 3, 0.263158f, 0xef, v_emit, 6);
    /* 0.99.1: ids 0..255 */
    put_matl(&main_c, 0, "_type=_diffuse;_weight=1;_rough=0.1;_spec=0.5;_ior=0.3");
    put_matl(&main_c, 95, "_type=_metal;_weight=0.252632;_rough=0.305263;_spec=0.642105;_ior=0.3");
    put_matl(&main_c, 155, "_type=_diffuse;_weight=1;_plastic=1;_rough=0.1;_spec=0.663158;_ior=0.3");
    put_matl(&main_c, 247, "_type=_emit;_weight=0.231579;_rough=0.1;_spec=0.5;_ior=0.3;_flux=2;_glow=0.315789");
    put_matl(&main_c, 252, "_type=_glass;_weight=0.431579;_rough=0.326316;_spec=0.5;_ior=0.378947;_att=0.410526");
    /* 0.99.4.2: ids 1..256 */
    put_matl(&main_c, 248,
             "_type=_metal;_weight=0.25;_rough=0.34;_spec=0.63;_spec_p=0.5;_ior=0.3;_att=0;_g0=-0.5;_g1=0.8;_gw=0.7;"
             "_flux=0;_ldr=0");
    put_matl(&main_c, 250,
             "_type=_plastic;_weight=0.28;_rough=0.42;_spec=0.5;_spec_p=0.7;_ior=0.3;_att=0;_g0=-0.5;_g1=0.8;_gw=0.7;"
             "_flux=0;_ldr=0");
    put_matl(&main_c, 197,
             "_type=_emit;_weight=0.28;_rough=0.1;_spec=0.5;_spec_p=0.5;_ior=0.3;_att=0;_g0=-0.5;_g1=0.8;_gw=0.7;"
             "_flux=2;_ldr=0.27");
    put_matl(&main_c, 249,
             "_type=_media;_weight=0.33;_rough=0.1;_spec=0.5;_spec_p=0.5;_ior=0.3;_att=0;_g0=-0.25;_g1=0.9;_gw=0.53;"
             "_flux=0;_ldr=0");
    put_matl(&main_c, 251,
             "_type=_glass;_weight=0.26;_rough=0.26;_spec=0.5;_spec_p=0.5;_ior=0.65;_att=0.34;_g0=-0.5;_g1=0.8;_gw=0.7;"
             "_flux=0;_ldr=0");
    put_matl(&main_c, 10,
             "_type=_media;_weight=0.01;_rough=0.1;_spec=0.5;_spec_p=0.5;_ior=0.3;_att=0;_g0=-0.5;_g1=0.8;_gw=0.7;"
             "_flux=0;_ldr=0");
    put_matl(&main_c, 11, "_type=_media;_weight=0.05");
    put_matl(&main_c, 12, "_type=_media;_weight=0.1");
    put_matl(&main_c, 13, "_type=_media;_weight=0.2");
    /* glass: _d from _att by the same curve, 0 for _att 0 */
    put_matl(&main_c, 20, "_type=_glass;_weight=0.05;_att=0");
    put_matl(&main_c, 21, "_type=_glass;_weight=0.05;_att=0.05");
    put_matl(&main_c, 22, "_type=_glass;_weight=0.05;_att=0.2");
    put_matl(&main_c, 23, "_type=_glass;_weight=0.05;_att=1");
    put_matl(&main_c, 24, "_type=_media;_weight=0.22");
    /* current files: a MATL without _d has density 0 */
    put_matl(&main_c, 30, "_type=_glass;_rough=0.1;_ior=0.3;_ri=1.3");
    /* a current key wins over _weight */
    put_matl(&main_c, 100, "_type=_metal;_weight=0.9;_metal=0.4;_sp=1.5;_spec=0.1");
    /* 0.99.1 background switched off */
    put_dict_chunk(&main_c, "rOBJ", 0, 0, "_type=_bg;_color=10 20 30;_enable=0");

    put(&file, "VOX ", 4);
    put_i32(&file, 150);
    put(&file, "MAIN", 4);
    put_i32(&file, 0);
    put_i32(&file, (int32_t)main_c.n);
    put(&file, main_c.p, main_c.n);

    CHECK(v4c_load_memory(file.p, file.n, NULL, &s) == V4C_SUCCESS && s, "legacy material file");
    if (s)
    {
        const v4c_material* m = s->materials;
        /* MATT glass: weight = transparency, ior stored as n - 1, specular only for metals */
        CHECK(m[9].type == V4C_MATERIAL_GLASS && NEAR(m[9].transparency, 0.305263f) && NEAR(m[9].ior, 1.505263f) &&
                  NEAR(m[9].roughness, 0.378947f) && NEAR(m[9].specular, 1.0f) && NEAR(m[9].attenuation, 0.326316f) &&
                  NEAR(m[9].density, 0.1f) && !m[9].plastic,
              "MATT glass");
        /* MATT emit: weight = _emit, power = _flux, glow dropped */
        CHECK(m[3].type == V4C_MATERIAL_EMIT && NEAR(m[3].emissive, 0.263158f) && NEAR(m[3].emission_power, 2.0f) &&
                  NEAR(m[3].ldr, 0.0f),
              "MATT emit: emit %g flux %g ldr %g", m[3].emissive, m[3].emission_power, m[3].ldr);
        /* 0.99.1 writes an id 0, which 0.99.7 ignores: index 0 keeps the defaults of a material without MATL */
        CHECK(m[0].type == V4C_MATERIAL_DIFFUSE && NEAR(m[0].density, 0.05f) && NEAR(m[0].weight, 1.0f),
              "0.99.1 id 0 ignored");
        CHECK(m[95].type == V4C_MATERIAL_METAL && NEAR(m[95].metallic, 0.252632f) && NEAR(m[95].specular, 1.642105f) &&
                  NEAR(m[95].roughness, 0.305263f),
              "0.99.1 metal: metal %g spec %g", m[95].metallic, m[95].specular);
        /* 0.99.1 plastic: 0.99.7 makes it a plain diffuse material */
        CHECK(m[155].type == V4C_MATERIAL_DIFFUSE && m[155].plastic && NEAR(m[155].specular, 1.0f), "0.99.1 plastic");
        CHECK(NEAR(m[247].emissive, 0.231579f) && NEAR(m[247].ldr, 0.0f) && NEAR(m[247].emission_power, 2.0f),
              "0.99.1 emit");
        CHECK(m[252].type == V4C_MATERIAL_GLASS && NEAR(m[252].transparency, 0.431579f) &&
                  NEAR(m[252].ior, 1.378947f) && NEAR(m[252].attenuation, 0.410526f) && NEAR(m[252].density, 0.1f),
              "0.99.1 glass");
        /* 0.99.4: the two phase lobes become _g = 0.7 * 0.8 + 0.3 * -0.5 */
        CHECK(NEAR(m[248].metallic, 0.25f) && NEAR(m[248].specular, 1.63f) && NEAR(m[248].phase, 0.41f),
              "0.99.4 metal");
        /* 0.99.4 plastic: a metal without metalness, _ior = _spec_p */
        CHECK(m[250].type == V4C_MATERIAL_METAL && m[250].plastic && NEAR(m[250].metallic, 0.0f) &&
                  NEAR(m[250].ior, 1.7f) && NEAR(m[250].specular, 1.0f) && NEAR(m[250].roughness, 0.42f) &&
                  NEAR(m[250].weight, 0.28f),
              "0.99.4 plastic: type %d ior %g", (int)m[250].type, m[250].ior);
        CHECK(NEAR(m[197].emissive, 0.28f) && NEAR(m[197].ldr, 0.27f), "0.99.4 emit");
        CHECK(m[249].type == V4C_MATERIAL_MEDIA && m[249].media_type == V4C_MEDIA_SCATTER &&
                  NEAR(m[249].density, 0.1f) && NEAR(m[249].phase, 0.3595f) && NEAR(m[249].weight, 0.33f),
              "0.99.4 media: phase %g", m[249].phase);
        CHECK(m[251].type == V4C_MATERIAL_GLASS && NEAR(m[251].transparency, 0.26f) && NEAR(m[251].ior, 1.65f) &&
                  NEAR(m[251].density, 0.1f) && NEAR(m[251].phase, 0.41f),
              "0.99.4 glass");
        CHECK(NEAR(m[100].metallic, 0.4f) && NEAR(m[100].specular, 1.5f), "current keys win");
        CHECK(s->render.has_background && !s->render.show_background && s->render.background.r == 10, "_bg _enable 0");
        CHECK(m[10].type == V4C_MATERIAL_MEDIA && NEAR(m[10].density, 0.00400803f), "0.99.4 thin medium: d %g",
              m[10].density);
        CHECK(NEAR(m[20].density, 0.0f) && NEAR(m[21].density, 0.0202027f) && NEAR(m[22].density, 0.0833816f) &&
                  NEAR(m[23].density, 0.1f) && NEAR(m[24].density, 0.0921153f),
              "0.99.4 glass / media densities: %g %g %g %g %g", m[20].density, m[21].density, m[22].density,
              m[23].density, m[24].density);
        CHECK(m[30].type == V4C_MATERIAL_GLASS && NEAR(m[30].density, 0.0f), "MATL without _d: %g", m[30].density);
        CHECK(NEAR(m[11].density, 0.0202027f) && NEAR(m[12].density, 0.040822f) && NEAR(m[13].density, 0.0833816f),
              "0.99.4 media densities: %g %g %g", m[11].density, m[12].density, m[13].density);
        v4c_free_scene(s);
    }
    free(main_c.p);
    free(file.p);

    /* 0.99.4 and later: the switch is _setting _bg_c, in a group of its own */
    {
        static const char* const robj[2][2] = { { "_type=_bg;_color=163 82 82", "_type=_setting;_bg_c=1;_bg_a=1" },
                                                { "_type=_setting;_bg_c=0;_bg_a=1", "_type=_bg;_color=163 82 82" } };
        int                      k;
        for (k = 0; k < 2; ++k)
        {
            size_t         size;
            unsigned char* data = make_render_file(robj[k], 2, NULL, 0, &size);
            s                   = NULL;
            CHECK(v4c_load_memory(data, size, NULL, &s) == V4C_SUCCESS && s, "background file");
            if (s)
                CHECK(s->render.has_background && s->render.background.r == 163 &&
                          s->render.show_background == (k == 0),
                      "_setting _bg_c = %d", k == 0);

            v4c_free_scene(s);
            free(data);
        }
    }
}

int main(int argc, char** argv)
{
    int              i;
    v4c_scene*       s = NULL;
    v4c_load_options opt;

    CHECK(v4c_load_memory("VOX", 3, NULL, &s) == V4C_ERROR_INVALID_FORMAT, "short file");
    CHECK(v4c_load_memory("ABCD\0\0\0\0", 8, NULL, &s) == V4C_ERROR_INVALID_FORMAT, "bad magic");
    CHECK(v4c_load_file("does/not/exist.vox", NULL, &s) == V4C_ERROR_FILE_IO, "missing file");
    v4c_default_load_options(&opt);
    opt.coord_system   = V4C_COORDS_CUSTOM;
    opt.custom_axes[0] = V4C_AXIS_POS_X;
    opt.custom_axes[1] = V4C_AXIS_NEG_X;
    opt.custom_axes[2] = V4C_AXIS_POS_Z;
    CHECK(v4c_load_memory("VOX \x96\0\0\0", 8, &opt, &s) == V4C_ERROR_INVALID_ARGUMENT, "invalid axes");
    v4c_default_load_options(&opt);
    opt.scale = 0.0f;
    CHECK(v4c_load_memory("VOX \x96\0\0\0", 8, &opt, &s) == V4C_ERROR_INVALID_ARGUMENT, "invalid scale");
    CHECK(v4c_load_memory("VOX \x96\0\0\0", 8, NULL, &s) == V4C_SUCCESS && s && s->num_models == 0, "empty file");
    v4c_free_scene(s);

    CHECK(sizeof(v4c_packed_vertex) == 8 && offsetof(v4c_packed_vertex, normal) == 6 &&
              offsetof(v4c_packed_vertex, material) == 7,
          "v4c_packed_vertex layout (the GLSL example relies on it)");
    test_materials();
    test_render_settings();
    test_light_file();
    test_lenses();
    test_legacy_files();
    test_legacy_materials();

    for (i = 1; i < argc; ++i)
        test_file(argv[i]);

    printf(g_failures ? "\n%d FAILURE(S)\n" : "\nall tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}
