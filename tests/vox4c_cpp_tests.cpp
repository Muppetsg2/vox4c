// Checks that vox4c.h works from C++: built with the implementation compiled
// as C++ and, linked against vox4c::lib, with the C implementation.
#ifndef VOX4C_PREBUILT
#define VOX4C_IMPLEMENTATION
#endif
#include "vox4c.h"

#include <cstdio>
#include <cstring>

int main()
{
    int failures = 0;
#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        if (!(cond))                                                    \
        {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

    // "VOX " version 150, empty MAIN chunk
    const unsigned char file[] = { 'V', 'O', 'X', ' ', 150, 0, 0, 0, 'M', 'A', 'I', 'N', 0, 0, 0, 0, 0, 0, 0, 0 };

    v4c_load_options opt;
    v4c_default_load_options(&opt);
    opt.flags |= V4C_FLAG_PACKED_VERTICES | V4C_FLAG_USED_MATERIALS_ONLY;
    opt.coord_system = V4C_COORDS_UNITY;

    v4c_scene*       scene = NULL;
    const v4c_result res   = v4c_load_memory(file, sizeof(file), &opt, &scene);
    CHECK(res == V4C_SUCCESS);
    CHECK(scene != NULL);
    if (scene)
    {
        CHECK(scene->num_models == 0);
        CHECK(scene->left_handed);
        v4c_free_scene(scene);
    }
    CHECK(v4c_load_memory(file, 3, &opt, &scene) == V4C_ERROR_INVALID_FORMAT);
    CHECK(std::strlen(v4c_result_string(V4C_ERROR_FRAME_NOT_FOUND)) > 0);
    CHECK(sizeof(v4c_packed_vertex) == 8);

    std::printf(failures ? "%d FAILURE(S)\n" : "C++ build OK\n", failures);
    return failures ? 1 : 0;
}
