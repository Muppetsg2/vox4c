# vox4c

Single-header C99 library for loading [MagicaVoxel](https://ephtracy.github.io/) `.vox` files, in the spirit of
tinyobjloader / tinygltf. Works from C and C++, has no dependencies beyond the C standard library (not even `libm`).

- models, palette and materials (`MATL`, legacy `MATT`)
- files of every MagicaVoxel version from before 0.98.2 to 0.99.7 (the version number in the header is 150 up to
  0.99.6 and 200 since 0.99.7); older materials are converted the way MagicaVoxel 0.99.7 converts them
- the full scene graph (`nTRN` / `nGRP` / `nSHP`) exposed as a simple node tree that maps 1:1 onto engine entities
- layers (`LAYR`) with name, colour and hidden flag
- lighting and background of the MagicaVoxel render (sun as directional or area light, sky light, lens) and the
  camera slots
- animation: transform and model keyframes, `_loop`, the editor's timeline (`META _anim_range`), legacy 0.98 `PACK`
  animations; sampling at fractional frames (MagicaVoxel's rules or smooth interpolation), the list of models drawn
  at a frame with their matrices, whole frames baked into one mesh, and loading of a single frame
- optional monotone meshing (0fps / mik14a VoxImporter algorithm), with submeshes per material, as float arrays or
  as 8 byte packed vertices, with 32 or 16-bit indices, while loading or on demand per model
- optional "used materials only" compaction of the 256 entry palette
- selectable output coordinate system (MagicaVoxel / Unreal / OpenGL / Unity / custom), scale and pivot

A description of the `.vox` format, including everything learned while writing this library (renderer settings,
cameras, lenses, defaults), is in [docs/VoxFileFormat.md](docs/VoxFileFormat.md): part I covers the current
format (MagicaVoxel 0.99.7), part II what older versions write differently.

## Quick start

```c
#define VOX4C_IMPLEMENTATION   /* in exactly one .c / .cpp file */
#include "vox4c.h"

v4c_load_options opt;
v4c_default_load_options(&opt);
opt.flags        |= V4C_FLAG_GENERATE_MESHES | V4C_FLAG_USED_MATERIALS_ONLY;
opt.coord_system  = V4C_COORDS_UNITY;
opt.scale         = 0.1f;             /* one voxel = 0.1 units */

v4c_scene* scene = NULL;
v4c_result res = v4c_load_file("castle.vox", &opt, &scene);   /* or v4c_load_memory */
if (res != V4C_SUCCESS) {
    fprintf(stderr, "%s\n", v4c_result_string(res));
    return 1;
}
/* ... use scene ... */
v4c_free_scene(scene);
```

Configuration macros (define before the implementation): `VOX4C_STATIC` (static functions), `V4C_MALLOC` /
`V4C_REALLOC` / `V4C_FREE` (custom allocator, define all three) and `V4C_NO_STDIO` (removes `v4c_load_file`).

## Load options

| field | default | meaning |
|---|---|---|
| `flags` | `0` | `V4C_FLAG_GENERATE_MESHES`, `V4C_FLAG_PACKED_VERTICES`, `V4C_FLAG_USED_MATERIALS_ONLY`, `V4C_FLAG_DISCARD_VOXELS`, `V4C_FLAG_CLOCKWISE_WINDING`, `V4C_FLAG_16BIT_INDICES` |
| `frame` | `V4C_ALL_FRAMES` | load the whole animation, or only frame `N`, up to the last keyframe or the end of the timeline, whichever is later (`V4C_ERROR_FRAME_NOT_FOUND` past it) |
| `coord_system` | `V4C_COORDS_Z_UP_RIGHT_HANDED` | output coordinates, see below |
| `custom_axes[3]` | – | for `V4C_COORDS_CUSTOM`: output x / y / z = signed MagicaVoxel axis |
| `scale` | `1.0` | size of one voxel in output units |
| `pivot` | `V4C_PIVOT_MAGICAVOXEL` | origin of model meshes: `MAGICAVOXEL` (floor(size/2)), `CORNER`, `CENTER`, `BOTTOM_CENTER` |

### Materials

Without `V4C_FLAG_USED_MATERIALS_ONLY` the scene always contains all 256 materials and a material index equals the
palette index (index 0 is the "empty" slot and is never referenced by voxels). With the flag only materials referenced
by at least one voxel of a loaded model are kept; voxel and mesh material indices are remapped to the compacted list
and `v4c_material::palette_index` keeps the original index.

| field | MATL key | default | used by (in MagicaVoxel) |
|---|---|---|---|
| `type` | `_type` | diffuse | |
| `roughness` | `_rough` | 0.1 | all surfaces |
| `metallic` | `_metal` (older: `_weight` of a metal) | 0.0 | metal, blend |
| `specular` | `_sp` (older metal: `_spec` + 1) | 1.0 | metal, blend |
| `ior` | `_ior + 1`, or `_ri` | 1.3 | glass, blend |
| `transparency` | `_trans`, or `_alpha` (older: `_weight` of a glass) | 0.0 | glass, blend |
| `emissive` | `_emit` (older: `_weight` of an emitter) | 0.0 | emit |
| `emission_power` | `_flux` | 0.0 | emit |
| `ldr` | `_ldr` | 0.0 | emit |
| `media_type` | `_media_type`, or `_media` (0–3) | absorb | glass, blend, media |
| `density` | `_d` (0 when a `MATL` leaves it out) | 0.05 | glass, blend, media |
| `phase` | `_g` (Henyey–Greenstein g; older: from `_g0`, `_g1`, `_gw`) | 0.0 | glass, blend, media |
| `weight` | `_weight` as stored (older versions) | 1.0 | |
| `attenuation` | `_att` (older versions) | 1.0 | |
| `plastic` | `_plastic`, `_type _plastic` (older versions) | false | |

Material types: `V4C_MATERIAL_DIFFUSE` (`_diffuse` or no `_type` key), `METAL` (`_metal`), `GLASS` (`_glass`),
`EMIT` (`_emit`), `BLEND` (`_blend`, a mix of metal and glass parameters) and `MEDIA` (`_media`, a participating
medium / cloud without a surface). Media types: `V4C_MEDIA_ABSORB` (default), `SCATTER` (`_scatter`), `EMIT`
(`_emit`) and `SSS` (`_sss`, subsurface scattering).

`ior` is the physical index of refraction (glass ≈ 1.5). MagicaVoxel writes it twice: `_ri` = n and `_ior` = n − 1,
vox4c converts the latter. MagicaVoxel omits keys that are at their default, so a missing key means the default
above. Every key is read regardless of the material type; which ones matter depends on the type (last column).
Numbers are parsed locale-independently.

Materials of older MagicaVoxel versions (0.98 `MATT` chunks, the `MATL` keys of 0.99.1 – 0.99.4) are converted the
way MagicaVoxel 0.99.7 converts them when it opens such a file: the per-type `_weight` becomes `metallic`,
`transparency` or `emissive`, a metal's `_spec` becomes `specular` + 1, the plastic type of 0.99.4 becomes a metal
without metalness, the two phase lobes of 0.99.4 become one `phase`, and so on. This was checked against files
re-saved by 0.99.7: all 256 materials come out identical. The rules are in part II of
[docs/VoxFileFormat.md](docs/VoxFileFormat.md).

`v4c_palette_texture()` writes a `num_materials x 1` RGBA8 texture; mesh texcoords sample the centre of the texel of
their material, so a mesh can be rendered with a single material + palette texture as well as with submeshes.

## Coordinate systems

MagicaVoxel is right handed and Z-up; the front of a model faces −Y. Each preset keeps "up" up and turns the model's
front towards the target's conventional front. `out[i] = sign * magicavoxel[axis]`:

| preset | aliases | output (x, y, z) | handedness |
|---|---|---|---|
| `V4C_COORDS_Z_UP_RIGHT_HANDED` | `MAGICAVOXEL`, `BLENDER` | ( x, y, z) | right |
| `V4C_COORDS_Z_UP_LEFT_HANDED` | `UNREAL` | (−y, −x, z), front → +X | left |
| `V4C_COORDS_Y_UP_RIGHT_HANDED` | `OPENGL`, `GLTF`, `GODOT` | ( x, z, −y), front → +Z | right |
| `V4C_COORDS_Y_UP_LEFT_HANDED` | `UNITY` | (−x, z, −y), front → +Z | left |
| `V4C_COORDS_CUSTOM` | – | `custom_axes` (must be a permutation) | computed |

Everything is converted: voxel grids (`v4c_model::size`, voxel coordinates), mesh positions and normals, pivots,
translations and rotations (`R' = C R Cᵀ`). The mapping used is stored in `scene->axes` / `scene->left_handed`.

Triangles are counter-clockwise around the outward normal in the output coordinates (`cross(v1 - v0, v2 - v0)`
points outwards), which is what OpenGL, glTF, Godot, Unity and Unreal expect for front faces once the data is in their
own axes. `V4C_FLAG_CLOCKWISE_WINDING` flips it.

Matrices are `float[16]`, column-major, column vectors (`p' = M * p`, glm / OpenGL layout). `v4c_transform::rotation`
is a row-major 3x3 matrix.

## Scene graph

MagicaVoxel stores `Transform → Group | Shape` chains. vox4c collapses them into `v4c_node`s: each node is a transform
with children and / or a model. `nodes[0]` is the root. Shapes referenced from several places (MagicaVoxel instancing)
are expanded into separate nodes that share the same `v4c_model`. Files without a scene graph (saved by MagicaVoxel
0.98.2 and older) get a synthesized root with one child per model (one animated child for a `PACK`
animation), placed the way MagicaVoxel itself converts them: translation `(0, 0, floor(size.z / 2))`, so the model
stands on the ground. The node tree, transforms, palette and meshes of such a file are then identical to those of
the same file re-saved by a current MagicaVoxel.

A node carries `transform_keys` (at least one) and `model_keys` (none if the node has no model), both sorted by frame.
`v4c_transform` offers the rotation both as a matrix and as `quaternion * diag(scale)`, where `scale` is ±1 and only
negative when MagicaVoxel mirrors the model (the proper rotation lives in the quaternion) — handy for engines that store
TRS.

### Rebuilding a scene

```c
/* nodes are in pre-order: a parent always has a lower index than its children */
for (uint32_t i = 0; i < scene->num_nodes; ++i) {
    const v4c_node* n = &scene->nodes[i];
    Entity e = create_entity(n->name, n->parent >= 0 ? entities[n->parent] : NULL);
    entities[i] = e;

    v4c_transform t = v4c_sample_node_transform(scene, i, frame);   /* local TRS */
    set_local_trs(e, t.translation, t.quaternion, t.scale);
    set_visible(e, !n->hidden && (n->layer < 0 || !scene->layers[n->layer].hidden));

    int32_t m = v4c_sample_node_model(scene, i, frame);
    if (m >= 0) {
        const v4c_model* model = &scene->models[m];
        /* the mesh lives in model space: attach it through a child offset by scene_offset
           (zero with the default V4C_PIVOT_MAGICAVOXEL) */
        attach_mesh(e, upload_mesh(&model->mesh), model->scene_offset);
    }
}
```

Or, to bake: `v4c_node_model_matrix(scene, node, frame, M)` returns the world matrix of the node's mesh at `frame`,
and `v4c_node_is_visible()` combines hidden flags of the node, its ancestors and their layers. If the determinant of
the matrix is negative (a mirrored instance) flip the triangle winding. See `examples/vox2obj.c`, or let
`v4c_frame_instances()` / `v4c_bake_frame()` do it (below).

### Animation

A node animates its transform (`transform_keys`) and, as a flipbook, its model (`model_keys`: one model per pose).
`scene->num_frames` is the last keyframe + 1; `scene->anim_first .. anim_last` is the timeline of the editor
(`META _anim_range`); files without it get MagicaVoxel's default `0 .. 30` (extended to the last keyframe if that is
later), which is also what MagicaVoxel 0.99.7 writes when it re-saves an older file. The timeline can be longer than
the keys: looping nodes (`_loop`) repeat their key range, the others hold their last key.

Integer frames are sampled like MagicaVoxel does: rotations and models snap to the previous key, translations are
interpolated linearly and truncated to whole voxels. For playback at the engine's frame rate every function below
also takes a fractional `frame` and a `v4c_sampling`:

| sampling | transform between keys | model |
|---|---|---|
| `V4C_SAMPLE_MAGICAVOXEL` | exactly as MagicaVoxel shows frame `floor(frame)` | model key at `floor(frame)` |
| `V4C_SAMPLE_SMOOTH` | translation interpolated without truncation, rotation slerped (mirroring snaps); a looping node blends from its last key back to the first | model key at `floor(frame)` |

```c
/* what to draw at time t (seconds), 24 frames per second, looping over the timeline */
float span  = (float)(scene->anim_last - scene->anim_first + 1);
float frame = (float)scene->anim_first + fmodf(t * 24.0f, span);

v4c_instance inst[256];
uint32_t n = v4c_frame_instances(scene, frame, V4C_SAMPLE_SMOOTH, true /* visible only */, inst, 256);
for (uint32_t i = 0; i < n && i < 256; ++i) {
    /* inst[i].matrix: column-major world matrix of the model's mesh (scene_offset included),
       inst[i].mirrored: negative determinant, draw with the opposite face culling */
    draw(gpu_mesh[inst[i].model], inst[i].matrix, inst[i].mirrored);
}
```

`v4c_frame_instances()` returns the number of instances (call it with `NULL, 0` to only count them).
`v4c_sample_node_transform_at()` gives one node's local transform at a fractional frame, for engines that keep the
node hierarchy (see *Rebuilding a scene*).

`v4c_bake_frame(scene, frame, sampling, visible_only, flags, &mesh)` merges every model drawn at `frame` into one
world-space mesh (float arrays, one submesh per material, winding fixed for mirrored instances; `flags`:
`V4C_FLAG_CLOCKWISE_WINDING`, `V4C_FLAG_16BIT_INDICES`). Models without a mesh are meshed temporarily from their
voxels. Free it with `v4c_free_mesh()`. Good for exporters (`examples/vox4c_anim.c --obj`), collision shapes or
pre-baked vertex animation.

With `opt.frame = N` only frame `N` is loaded: each node keeps one key (the sampled state at `N`), models that no
node uses at that frame are dropped (`v4c_model::file_index` still tells which model of the file it is) and the
sampling functions ignore their `frame` argument.

#### Animations and memory

A flipbook animation is one model per pose, so meshing everything up front multiplies the mesh memory by the
number of poses although a frame draws only a few of them. Load without `V4C_FLAG_GENERATE_MESHES` (voxels only,
4 bytes per voxel) and build the meshes as frames need them:

```c
v4c_build_model_mesh(scene, model, V4C_FLAG_PACKED_VERTICES | V4C_FLAG_16BIT_INDICES);  /* -> model's mesh */
/* upload scene->models[model].mesh to the GPU ... */
v4c_free_model_mesh(scene, model);                                                       /* CPU copy no longer needed */
```

`v4c_build_model_mesh()` produces exactly the mesh `V4C_FLAG_GENERATE_MESHES` would (same coordinate system, scale,
pivot and palette mode). Upload all poses once and free the CPU copies, or keep only the meshes of the poses around
the current frame. For the animations in `vox_files/anim`, all poses meshed up front take 85 – 145 KB (packed
vertices, 32-bit indices), the poses of one frame 7 – 29 KB (packed vertices, 16-bit indices);
`examples/vox4c_anim.c` prints both for any file.

## Lighting, background and cameras

MagicaVoxel keeps the settings of its renderer (`rOBJ` chunks) and its camera slots (`rCAM`) in the file. vox4c
reads the ones needed to reproduce the look of a render in another program, converted to the output coordinate
system and scale, into `scene->render` and `scene->cameras`.

| field | from | meaning |
|---|---|---|
| `render.background`, `has_background` | `_bg` `_color` | background colour (the editor's) |
| `render.show_background` | `_setting` `_bg_c` | the render shows the background colour instead of the sky |
| `render.ambient_color`, `ambient_intensity`, `has_ambient` | `_uni` | uniform sky light |
| `render.fov`, `aperture`, `projection`, `has_lens` | `_lens` (`_len` in older files) | lens of the render: field of view (degrees), depth of field, projection (see *Lenses*) |
| `render.sun` | `_inf` | the sun, see below |

### The sun

`sun.present` tells whether the file has sun settings. `sun.direction` is the unit vector in which the light
travels (from the sun towards the scene), `sun.color` and `sun.intensity` its colour and strength. MagicaVoxel's
angles are kept as well: `elevation` above the horizon and `azimuth` (0 = in front of the model, 90 = right,
180 = behind, 270 = left).

The type depends on the sun's size (`_area`):

- `area == 0` → `V4C_LIGHT_DIRECTIONAL`: use `direction`, `color` and `intensity` for a directional light.
- `area > 0` → `V4C_LIGHT_AREA`: a sun of `angular_diameter` degrees gives soft shadows. `sun.area_light`
  describes it as a disk light far away, facing the scene, whose power lights the scene exactly as brightly as the
  directional light with the same `intensity` would (on the disk's axis):

  | `v4c_area_light` | |
  |---|---|
  | `position`, `normal`, `radius`, `distance` | disk centre, emitting direction (= `sun.direction`), size, distance to the target |
  | `power` | total flux, `pi * distance^2 * intensity / cos^2(half angle)` |
  | `intensity` | radiant intensity on the axis, `power / pi` |
  | `radiance` | emitted radiance, `intensity / (pi * sin^2(half angle))` |

  Pick whichever your engine's area light takes. The values are in the units of `sun.intensity`, so they scale
  together with it. By default the disk is placed around the centre of the scene (frame 0) at 100 times the
  scene's bounding radius; `v4c_sun_area_light(scene, target, distance, &light)` computes it for another target or
  distance, and `v4c_scene_bounds()` gives the bounds used for that.

### Cameras

MagicaVoxel does not store the live view of the editor or of the renderer in the file, only its camera slots
(normally 10, ids 0–9, sorted by id). A slot is written with default values until a view is saved into it; such
slots have `saved == false`. To carry a view over to another program, save it into a slot in MagicaVoxel and read
that slot:

```c
const v4c_camera* cam = &scene->cameras[1];
if (cam->saved) {
    float position[3], forward[3], up[3];
    v4c_camera_pose(scene, cam, position, forward, up);   /* output coordinates, unit vectors */
    set_camera_look_at(position, position + forward, up);
    /* vertical, degrees: the render's lens, else the slot's own value */
    set_camera_fov(scene->render.has_lens ? scene->render.fov : cam->fov);
}
```

`v4c_camera` also has the stored values: `mode` (perspective, free, panorama, orthographic, isometric), `focus`
(the point the camera orbits and looks at, converted), `radius` (its distance from the focus, scaled), `angle` and
`frustum`. MagicaVoxel's angles are: pitch (negative looks down, -45 sees the top and the front of a cube), yaw
(positive orbits the camera to the right, 90 looks at the +X side); the third value is not a roll (isometric slots
store e.g. 34 without any tilt) and is not used. This convention was checked against MagicaVoxel screenshots:
the poses of the front, right and top views and the framing of the front view match exactly.

- **Field of view:** `camera->fov` is the value saved with the slot; the MagicaVoxel render uses the lens setting
  `scene->render.fov`. To reproduce a render, use `render.fov` (in the test file above the render was made with
  33°, while the slots had been saved with 45°).
- **Orthographic / isometric:** `frustum` is half of the view height at the focus (scaled), so the orthographic
  size is `2 * frustum`; `radius` is just a large distance that keeps the camera outside the scene.
- **Perspective / free:** `frustum` is `tan(fov / 2)`.

### Lenses

`scene->render.projection` is MagicaVoxel's render lens (`_proj`, with the raw value in `projection_index`). All
three were measured on MagicaVoxel renders of a scene with markers in known directions:

| `v4c_projection` | `_proj` | image | `render.fov` |
|---|---|---|---|
| `V4C_PROJECTION_PERSPECTIVE` | 0 | pinhole | vertical field of view |
| `V4C_PROJECTION_STEREOGRAPHIC` | 1 | a point at angle θ from the viewing direction lands at `tan(θ/2) / tan(fov/4)` half-heights from the centre (conformal wide angle, almost the whole sphere) | vertical field of view |
| `V4C_PROJECTION_PANORAMA` | 2 | equirectangular, viewing direction in the centre: 180° down the height and the same degrees per pixel across (360° for a 2:1 image, 180° for a square one; wider images repeat the directions every 2 × height pixels) | not used |

`v4c_camera_ray()` gives the primary ray of any pixel exactly like MagicaVoxel (for raytracers and raymarching),
`v4c_camera_project()` the pixel of any point (overlays, picking; in a panorama wider than 2:1 it returns the copy
nearest to the image centre). Orthographic and isometric camera slots give
parallel rays; the lens does not apply to them.

```c
const v4c_camera* cam = &scene->cameras[0];
float origin[3], dir[3];
v4c_camera_ray(scene, cam, scene->render.projection, scene->render.fov,
               width, height, x + 0.5f, y + 0.5f, origin, dir);
```

In a rasterizing engine the perspective lens is an ordinary projection matrix with `render.fov` as the vertical
field of view. Stereographic and panorama images cannot be made with a projection matrix: render the scene into a
cubemap at the camera position and remap it with `examples/shaders/vox4c_lens.frag`, a fullscreen shader with the
same math as `v4c_camera_ray()` (checked against it on a GPU).

## Meshes

With `V4C_FLAG_GENERATE_MESHES` each model gets a `v4c_mesh` built by monotone meshing of every slice: coplanar
same-material faces are merged into monotone polygons and triangulated, which yields far fewer triangles than naive
cube faces or per-voxel quads. Vertices are not shared between polygons (flat normals, per-vertex material). Indices
are sorted by material and described by `submeshes`.

Positions are `(grid position − pivot) * scale`. Faces between two voxels of different colours are culled like between
equal ones (the outer shell only).

Meshes can also be built after loading, one model at a time, with `v4c_build_model_mesh()` (needs the voxels, so not
with `V4C_FLAG_DISCARD_VOXELS`) and freed with `v4c_free_model_mesh()`, see *Animations and memory*.

With `V4C_FLAG_16BIT_INDICES` a mesh with at most 65536 vertices stores its indices in `indices16` (`indices` is
`NULL`), halving the index memory; larger meshes keep 32-bit `indices`. `v4c_mesh_index(mesh, i)` reads either.
`mesh.clockwise` tells the winding of the triangles.

### Packed vertices

By default a mesh stores plain float arrays (33 bytes per vertex), which can be handed to any engine or to
`glBufferData` as they are. With `V4C_FLAG_PACKED_VERTICES` (implies `V4C_FLAG_GENERATE_MESHES`) every vertex is one
8 byte `v4c_packed_vertex` in `v4c_mesh::packed` instead, and `positions`, `normals`, `texcoords` and `materials`
are `NULL`. Indices, submeshes, winding and the triangle order are identical in both modes.

| offset | type | field | meaning |
|---:|---|---|---|
| 0 | `uint16_t[3]` | `x, y, z` | voxel-grid corner, 0..256, already in the output axes |
| 6 | `uint8_t` | `normal` | `v4c_axis`: 0 +X, 1 −X, 2 +Y, 3 −Y, 4 +Z, 5 −Z |
| 7 | `uint8_t` | `material` | index into `scene->materials` |

Decoding gives bit-identical values to the float arrays:

```
position = (xyz - model->pivot) * scene->scale
normal   = unit vector of the signed axis
texcoord = ((material + 0.5) / scene->num_materials, 0.5)
```

On the CPU use `v4c_unpack_vertex()`, or `v4c_mesh_vertex()`, which reads a vertex from either kind of mesh, so
import code can support both modes with one code path:

```c
for (uint32_t i = 0; i < model->mesh.num_vertices; ++i) {
    float p[3], n[3], uv[2];
    uint32_t material = v4c_mesh_vertex(scene, model, i, p, n, uv);
    /* ... */
}
```

On the GPU the packed array is uploaded as one interleaved buffer (OpenGL 3.0+ / GLES 3.0+ for integer attributes):

```c
const v4c_mesh* m = &model->mesh;
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, m->num_vertices * sizeof(v4c_packed_vertex), m->packed, GL_STATIC_DRAW);
glVertexAttribPointer (0, 3, GL_UNSIGNED_SHORT, GL_FALSE, 8, (void*)0); /* x, y, z  -> vec3 */
glVertexAttribIPointer(1, 1, GL_UNSIGNED_BYTE,            8, (void*)6); /* normal   -> uint */
glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE,            8, (void*)7); /* material -> uint */
glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);

glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
if (m->indices16)   /* V4C_FLAG_16BIT_INDICES */
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m->num_indices * sizeof(uint16_t), m->indices16, GL_STATIC_DRAW);
else
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m->num_indices * sizeof(uint32_t), m->indices, GL_STATIC_DRAW);

/* uniforms of examples/shaders/vox4c_packed.vert */
glUniform3fv(loc_pivot, 1, model->pivot);
glUniform1f (loc_scale, scene->scale);
glUniform1f (loc_num_materials, (float)scene->num_materials);
v4c_node_model_matrix(scene, node, frame, model_matrix);
glUniformMatrix4fv(loc_model, 1, GL_FALSE, model_matrix);   /* column-major, no transpose */

glDrawElements(GL_TRIANGLES, m->num_indices, m->indices16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, 0);
```

The attributes 1 and 2 must use `glVertexAttribIPointer` (with an `I`): the plain function would convert them to
floats and the shader's `uint` inputs would read garbage. A complete GLSL 3.30 vertex + fragment shader pair that
decodes the packed vertex and shades it with the palette texture is in `examples/shaders/vox4c_packed.vert` and
`examples/shaders/vox4c_packed.frag`. It was checked on Mesa's OpenGL driver to reproduce the float arrays exactly.

Which one to use: the float arrays are the simplest (any shader, any engine, no decoding); packed vertices need the
decoding shader or `v4c_mesh_vertex()` but use about 4× less vertex memory, roughly 2.7× less in total with indices.

#### Memory: float vs packed vertices

Mesh payload per file (vertex data + `uint32_t` indices + submesh records, allocator overhead not included).
The table rows are printed by `examples/vox4c_memory.c`:

```
cc -std=c99 -O2 examples/vox4c_memory.c -o vox4c_memory
./vox4c_memory path/to/*.vox              # add --compact to load with V4C_FLAG_USED_MATERIALS_ONLY
```

or, with CMake, `cmake --build build --target vox4c_memory_report` for every file in `vox_files/`, and
`cmake --build build --target vox4c_memory_readme` to replace the table below with that measurement (it is kept
between two HTML comments that do not show when the README is rendered).

<!-- vox4c:memory-table:begin -->
| file | vertices | triangles | vertex data (full) | vertex data (packed) | indices | total (full) | total (packed) | saving |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 3x3x3.vox | 168 | 96 | 5.4 KB | 1.3 KB | 1.1 KB | 6.6 KB | 2.4 KB | 2.67x |
| camera_test.vox | 24 | 12 | 0.8 KB | 0.2 KB | 0.1 KB | 0.9 KB | 0.3 KB | 2.72x |
| cars.vox | 1952 | 1066 | 62.9 KB | 15.2 KB | 12.5 KB | 75.6 KB | 28.0 KB | 2.70x |
| castle.vox | 4928 | 2758 | 158.8 KB | 38.5 KB | 32.3 KB | 191.1 KB | 70.8 KB | 2.70x |
| chr_knight.vox | 1980 | 1022 | 63.8 KB | 15.5 KB | 12.0 KB | 76.0 KB | 27.7 KB | 2.75x |
| doom.vox | 21404 | 11304 | 689.8 KB | 167.2 KB | 132.5 KB | 823.6 KB | 301.1 KB | 2.74x |
| light_test.vox | 24 | 12 | 0.8 KB | 0.2 KB | 0.1 KB | 0.9 KB | 0.3 KB | 2.72x |
| menger.vox | 682248 | 365040 | 21.47 MB | 5.21 MB | 4.18 MB | 25.65 MB | 9.38 MB | 2.73x |
| monu1.vox | 4976 | 2939 | 160.4 KB | 38.9 KB | 34.4 KB | 194.9 KB | 73.4 KB | 2.65x |
| monu2.vox | 20130 | 11657 | 648.7 KB | 157.3 KB | 136.6 KB | 785.4 KB | 293.9 KB | 2.67x |
| monu3.vox | 4204 | 2336 | 135.5 KB | 32.8 KB | 27.4 KB | 163.0 KB | 60.3 KB | 2.70x |
| one_color_palette_cube.vox | 24 | 12 | 0.8 KB | 0.2 KB | 0.1 KB | 0.9 KB | 0.3 KB | 2.72x |
| room.vox | 3872 | 2273 | 124.8 KB | 30.2 KB | 26.6 KB | 151.5 KB | 56.9 KB | 2.66x |
| sun_and_background.vox | 24 | 12 | 0.8 KB | 0.2 KB | 0.1 KB | 0.9 KB | 0.3 KB | 2.72x |
| teapot.vox | 85606 | 47173 | 2.69 MB | 668.8 KB | 552.8 KB | 3.23 MB | 1.19 MB | 2.71x |
| anim/deer.vox | 10014 | 5094 | 322.7 KB | 78.2 KB | 59.7 KB | 383.8 KB | 139.3 KB | 2.75x |
| anim/Gomez.vox | 5656 | 3074 | 182.3 KB | 44.2 KB | 36.0 KB | 219.3 KB | 81.2 KB | 2.70x |
| anim/T-Rex-m.vox | 9676 | 5666 | 311.8 KB | 75.6 KB | 66.4 KB | 378.7 KB | 142.5 KB | 2.66x |
| anim/T-Rex.vox | 9676 | 5666 | 311.8 KB | 75.6 KB | 66.4 KB | 378.7 KB | 142.5 KB | 2.66x |
| legacy/chr_knight_0.98.2.vox | 1980 | 1022 | 63.8 KB | 15.5 KB | 12.0 KB | 76.0 KB | 27.7 KB | 2.75x |
| legacy/chr_knight_0.99.1.vox | 1980 | 1022 | 63.8 KB | 15.5 KB | 12.0 KB | 76.0 KB | 27.7 KB | 2.75x |
| legacy/chr_knight_0.99.4.2.vox | 1980 | 1022 | 63.8 KB | 15.5 KB | 12.0 KB | 76.0 KB | 27.7 KB | 2.75x |
| legacy/chr_knight_0.99.6.4.vox | 1980 | 1022 | 63.8 KB | 15.5 KB | 12.0 KB | 76.0 KB | 27.7 KB | 2.75x |
| legacy/chr_knight_0.99.7.vox | 1980 | 1022 | 63.8 KB | 15.5 KB | 12.0 KB | 76.0 KB | 27.7 KB | 2.75x |
| older/3x3x3.vox | 168 | 96 | 5.4 KB | 1.3 KB | 1.1 KB | 6.6 KB | 2.4 KB | 2.67x |
| older/8x8x8.vox | 168 | 96 | 5.4 KB | 1.3 KB | 1.1 KB | 6.6 KB | 2.4 KB | 2.67x |
| older/castle.vox | 4928 | 2758 | 158.8 KB | 38.5 KB | 32.3 KB | 191.1 KB | 70.8 KB | 2.70x |
| older/chr_knight.vox | 1980 | 1022 | 63.8 KB | 15.5 KB | 12.0 KB | 76.0 KB | 27.7 KB | 2.75x |
| older/chr_old.vox | 534 | 286 | 17.2 KB | 4.2 KB | 3.4 KB | 20.7 KB | 7.7 KB | 2.70x |
| older/chr_rain.vox | 1858 | 953 | 59.9 KB | 14.5 KB | 11.2 KB | 71.2 KB | 25.8 KB | 2.76x |
| older/chr_sword.vox | 1294 | 669 | 41.7 KB | 10.1 KB | 7.8 KB | 49.7 KB | 18.1 KB | 2.75x |
| older/doom.vox | 21404 | 11304 | 689.8 KB | 167.2 KB | 132.5 KB | 823.6 KB | 301.1 KB | 2.74x |
| older/ephtracy.vox | 1596 | 818 | 51.4 KB | 12.5 KB | 9.6 KB | 61.1 KB | 22.2 KB | 2.76x |
| older/menger.vox | 682248 | 365040 | 21.47 MB | 5.21 MB | 4.18 MB | 25.65 MB | 9.38 MB | 2.73x |
| older/monu1.vox | 4976 | 2939 | 160.4 KB | 38.9 KB | 34.4 KB | 194.9 KB | 73.4 KB | 2.65x |
| older/monu10.vox | 20130 | 11657 | 648.7 KB | 157.3 KB | 136.6 KB | 785.4 KB | 293.9 KB | 2.67x |
| older/monu9.vox | 4204 | 2336 | 135.5 KB | 32.8 KB | 27.4 KB | 163.0 KB | 60.3 KB | 2.70x |
| older/nature.vox | 207744 | 114232 | 6.54 MB | 1.58 MB | 1.31 MB | 7.85 MB | 2.89 MB | 2.71x |
| older/shelf.vox | 1620 | 929 | 52.2 KB | 12.7 KB | 10.9 KB | 63.1 KB | 23.6 KB | 2.68x |
| older/teapot.vox | 85606 | 47173 | 2.69 MB | 668.8 KB | 552.8 KB | 3.23 MB | 1.19 MB | 2.71x |
| older/anim/deer.vox | 10014 | 5094 | 322.7 KB | 78.2 KB | 59.7 KB | 383.8 KB | 139.3 KB | 2.75x |
| older/anim/horse.vox | 13110 | 6795 | 422.5 KB | 102.4 KB | 79.6 KB | 503.2 KB | 183.1 KB | 2.75x |
| older/anim/T-Rex.vox | 9676 | 5666 | 311.8 KB | 75.6 KB | 66.4 KB | 378.7 KB | 142.5 KB | 2.66x |
| older/chr/chr_fox.vox | 2168 | 1173 | 69.9 KB | 16.9 KB | 13.7 KB | 83.7 KB | 30.8 KB | 2.72x |
| older/chr/chr_gumi.vox | 1954 | 995 | 63.0 KB | 15.3 KB | 11.7 KB | 74.7 KB | 27.0 KB | 2.76x |
| older/chr/chr_man.vox | 714 | 385 | 23.0 KB | 5.6 KB | 4.5 KB | 27.7 KB | 10.2 KB | 2.70x |
| older/chr/chr_poem.vox | 1024 | 527 | 33.0 KB | 8.0 KB | 6.2 KB | 39.3 KB | 14.3 KB | 2.75x |
<!-- vox4c:memory-table:end -->

## CMake

The repository ships a `CMakeLists.txt` (CMake 3.25+) with two targets:

| target | what it is | how to use it |
|---|---|---|
| `vox4c::vox4c` | header only (INTERFACE) | `#define VOX4C_IMPLEMENTATION` in exactly one source file before including `vox4c.h` |
| `vox4c::lib` | the implementation compiled once, always a **static** library (`VOX4C_BUILD_LIBRARY=ON`) | just `#include <vox4c.h>`, no define |

`vox4c::vox4c` is the default and the most flexible: the configuration macros (`V4C_MALLOC` / `V4C_REALLOC` /
`V4C_FREE`, `V4C_NO_STDIO`, `VOX4C_STATIC`) are set by you, in your own source file, and the code is compiled with
your flags. It enables no compiler language, so it also works in C++-only projects.

`vox4c::lib` removes the "where do I put the define" question. It is built as C99 regardless of the language of
your project, and it is always static: it is small, has no state worth sharing between modules, and a DLL would
need export macros for no benefit. Custom allocators are not available with it (they are compile-time macros of
the implementation); `V4C_NO_STDIO` is available as the `VOX4C_NO_STDIO` option. Code that links `vox4c::lib` gets
the definition `VOX4C_PREBUILT`, so defining `VOX4C_IMPLEMENTATION` there (which would duplicate every symbol)
stops with a clear `#error` instead of a wall of linker errors. The same applies to `VOX4C_STATIC`.

### CPM.cmake

```cmake
include(cmake/CPM.cmake)

CPMAddPackage(
    NAME vox4c
    GITHUB_REPOSITORY Muppetsg2/vox4c
    GIT_TAG v1.1.0
)

target_link_libraries(my_app PRIVATE vox4c::vox4c)
```

or, with the prebuilt static library:

```cmake
CPMAddPackage(
    NAME vox4c
    GITHUB_REPOSITORY Muppetsg2/vox4c
    GIT_TAG v1.1.0
    OPTIONS "VOX4C_BUILD_LIBRARY ON"
)

target_link_libraries(my_app PRIVATE vox4c::lib)
```

Tests, examples and the install rules are off when vox4c is not the top-level project, so nothing but the target is
added to your build. They can be switched on with `OPTIONS "VOX4C_BUILD_TESTS ON"`. `FetchContent` and
`add_subdirectory(vox4c)` work the same way.

### Installed package

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVOX4C_BUILD_LIBRARY=ON     # the option only for vox4c::lib
cmake --build build
cmake --install build --prefix /path/to/prefix
```

```cmake
find_package(vox4c 1.1.0 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE vox4c::vox4c)    # or vox4c::lib
```

This installs `vox4c.h` into `include/`, the package files into `share/cmake/vox4c/`, the README into the doc
folder and, with `VOX4C_BUILD_LIBRARY`, `libvox4c.a` / `vox4c.lib` into `lib/`. The Debug build is named
`vox4cd`, so installing the Debug and the Release configuration into the same prefix gives a package that picks
the matching one automatically. The package variables `vox4c_HAS_LIBRARY` and `vox4c_MSVC_RUNTIME` tell what was
installed.

### Options

| option | default | meaning |
|---|---|---|
| `VOX4C_BUILD_LIBRARY` | OFF | build the static library `vox4c::lib` |
| `VOX4C_NO_STDIO` | OFF | `vox4c::lib` without `<stdio.h>` (no `v4c_load_file`, propagated to its users) |
| `VOX4C_MSVC_RUNTIME` | `DEFAULT` | MSVC C runtime of vox4c's targets: `MD` (`/MD`, Debug `/MDd`), `MT` (`/MT`, Debug `/MTd`), or `DEFAULT` = whatever `CMAKE_MSVC_RUNTIME_LIBRARY` of your project says |
| `VOX4C_BUILD_TESTS` | ON if top-level | build `vox4c_tests`, `vox4c_cpp_tests`, the misuse check and the CTest tests |
| `VOX4C_BUILD_EXAMPLES` | ON if top-level | build `vox4c_info`, `vox2obj`, `vox4c_memory` |
| `VOX4C_INSTALL` | ON if top-level | generate the install rules and the `find_package` config |
| `VOX4C_SANITIZE` | OFF | build vox4c's targets with AddressSanitizer + UBSan (MSVC: AddressSanitizer) |
| `VOX4C_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` for vox4c's own targets |
| `VOX4C_ENABLE_CLANG_TIDY` | OFF | clang-tidy targets for vox4c's own targets, see *Code style and static analysis* |
| `VOX4C_VS_LAUNCH_CONFIG` | ON on Windows if top-level | write `.vs/launch.vs.json`, see *Visual Studio* |
| `VOX4C_VOX_FILES_DIR` | `<source>/vox_files` | folder searched recursively for `*.vox` by the targets below |
| `VOX4C_FILE_TIMEOUT` | 600 | seconds allowed for one `.vox` file in those targets |

### Compiler and linker settings of `vox4c::lib`

All of them are private to the library; the only thing its users receive is the include path and the
`VOX4C_PREBUILT` definition (plus `V4C_NO_STDIO` if enabled). The library has no link dependencies, not even
`libm`.

| setting | compilers | why |
|---|---|---|
| `STATIC`, independent of `BUILD_SHARED_LIBS` | all | nothing to gain from a DLL / .so |
| C99, no extensions | all | same code on every compiler |
| position independent code (`-fPIC`) | GCC, Clang | the library can be linked into PIE executables and into shared libraries |
| hidden symbol visibility | GCC, Clang (ELF, Mach-O) | when it ends up inside your shared library, vox4c's functions are not exported from it, so two such libraries cannot clash; direct linking is not affected |
| `-ffunction-sections -fdata-sections` | GCC, Clang | the linker can drop unused functions (`--gc-sections`, `-dead_strip`) |
| `/Gy /Gw` | MSVC, clang-cl | the same for the MSVC linker (`/OPT:REF`) |
| debug info embedded in the objects (`/Z7`) | MSVC | no separate PDB, so users of the installed `.lib` do not get LNK4099 |
| `/MD` or `/MT` from `VOX4C_MSVC_RUNTIME` | MSVC, clang-cl | a static library must use the same C runtime as the program (LNK2038 / LNK4098 otherwise); Debug builds automatically use `/MDd` / `/MTd` |
| no link-time optimisation | all | LTO objects in a static library only link with the exact same compiler version |
| `d` postfix for Debug | all | Debug and Release can be installed side by side |
| `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` / `/W4`, `_CRT_SECURE_NO_WARNINGS` | per compiler | the library is kept warning free; `fopen` is portable C |

With the MSVC runtime the rule is simply: build vox4c with the same `/MD` / `/MT` choice as your program. When
vox4c is added with CPM / `add_subdirectory`, `DEFAULT` already follows your project's
`CMAKE_MSVC_RUNTIME_LIBRARY`. For an installed package `find_package` prints a warning when your project's
`CMAKE_MSVC_RUNTIME_LIBRARY` clearly does not match the runtime the library was built with.

### Visual Studio

With Visual Studio's *Open Folder* CMake support (File > Open > Folder), vox4c writes `.vs/launch.vs.json`, so the
tests and examples appear in the **Startup Item** dropdown ready to run and debug:

| configuration | arguments |
|---|---|
| `vox4c_tests (built-in tests)`, `vox4c_tests_lib (built-in tests)` | none |
| `vox4c_tests (vox_files)`, `vox4c_tests_lib (vox_files)` | every `.vox` file in `vox_files/` |
| `vox4c_cpp_tests`, `vox4c_cpp_tests_lib` | none |
| `vox4c_info (<file>)`, `vox2obj (<file>)` | the first `.vox` file in `vox_files/` (`vox2obj` writes the OBJ into the build folder) |
| `vox4c_memory (vox_files)` | every `.vox` file in `vox_files/` |

The `_lib` entries exist with `VOX4C_BUILD_LIBRARY=ON`; the `vox_files` and example entries once `vox_files/`
contains files. On Windows CMake re-runs by itself when files are added there. The generator is
`cmake/vs_launch.cmake` (`add_vs_launch_config()` / `write_vs_launch_json()`); it is used only when vox4c is the
top-level project, and can be turned off with `-DVOX4C_VS_LAUNCH_CONFIG=OFF`. Solutions made with the Visual Studio
generator (`.sln`) do not read `launch.vs.json`.

### Code style and static analysis

`.clang-format` describes the code style (4 spaces, Allman braces, 120 columns, `T* p`). The sources predate it and
use hand-aligned comment columns in the public structs, so they are not reformatted wholesale; use it for new code
(`clang-format -i file.c`, or format-on-save in an editor).

`.clang-tidy` enables the checks that apply to C (`bugprone-`, `cert-`, `clang-analyzer-`, `misc-`, `performance-`,
`portability-`, `readability-`); every disabled check is explained in the file. The library, the tests and the
examples pass it without warnings (clang-tidy 18).

```sh
cmake -S . -B build -G Ninja -DVOX4C_ENABLE_CLANG_TIDY=ON -DVOX4C_BUILD_LIBRARY=ON
cmake --build build --target vox4c_clang_tidy        # files changed since the last clean run
cmake --build build --target vox4c_clang_tidy_full   # everything
```

Each checked target `T` also gets `clang_tidy_T` and `clang_tidy_T_full`: `vox4c_tests`, `vox4c_cpp_tests`,
`vox4c_info`, `vox2obj`, `vox4c_memory` and, with `VOX4C_BUILD_LIBRARY`, `vox4c_lib`, `vox4c_tests_lib`,
`vox4c_cpp_tests_lib`. The library code lives in `vox4c.h` and is checked, as C99, through the C files that define
`VOX4C_IMPLEMENTATION`; the C++ tests are checked as C++11, their own code only (the C header compiled as C++ would
only report C++-only style points). `vox4c_misuse_check` is not checked: it is meant not to compile, so clang-tidy
can only report its `#error`. Requirements: clang-tidy and the Ninja or a Makefile generator
(`compile_commands.json`); the option is ignored when vox4c is not the top-level project.

The incremental targets remember files by the hash of the `.c` file and the configuration only: after a change
in `vox4c.h` alone, run the `_full` target.

### Running on your .vox files

Put any number of `.vox` files into `vox_files/`, in any sub-folder structure. The targets below search the folder
when they run, so new files are picked up without re-running CMake:

| target | what it does |
|---|---|
| `vox4c_run_tests` | built-in tests, then `vox4c_tests` on every file; one `[ OK ]` / `[FAIL]` line per file, fails if any file fails |
| `vox4c_run_tests_verbose` | the same with the full output of every run |
| `vox4c_run_tests_lib` | the same tests built against `vox4c::lib` (with `VOX4C_BUILD_LIBRARY`) |
| `vox4c_run_misuse_check` | checks that `VOX4C_IMPLEMENTATION` next to `vox4c::lib` is rejected with the `#error` |
| `vox4c_run_info` | `vox4c_info --compact` on every file (layers, models, materials, node tree) |
| `vox4c_memory_report` | the float vs packed memory table for every file, printed and written to `<build>/vox4c_memory.md` |
| `vox4c_memory_readme` | the same table, written into `README.md` in place of the previous one |
| `vox4c_memory_report_compact` | the same with `V4C_FLAG_USED_MATERIALS_ONLY` (`<build>/vox4c_memory_compact.md`) |
| `vox4c_export_obj` | converts every file to OBJ + MTL into `<build>/obj/`, keeping the sub-folders |
| `vox4c_run_anim` | `vox4c_anim --smooth --step 0.5` on every file: timeline, instances per frame, mesh memory up front vs on demand |
| `vox4c_validate_shaders` | compiles and links the example GLSL shaders (only if `glslangValidator` is found) |

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --build build --target vox4c_run_tests
cmake --build build --target vox4c_memory_report
ctest --test-dir build --output-on-failure
```

CTest runs `vox4c.builtin`, `vox4c.vox_files` (all files of `VOX4C_VOX_FILES_DIR`), `vox4c.cpp` (the header
compiled and run as C++11), `vox4c.misuse_is_reported` and, when `glslangValidator` is available, `vox4c.shaders`.
With `VOX4C_BUILD_LIBRARY` the same tests also run against `vox4c::lib` (`vox4c.lib.*`, from C and from C++).

`vox4c.misuse_is_reported` (also `cmake --build build --target vox4c_run_misuse_check`) builds
`vox4c_misuse_check`, which defines `VOX4C_IMPLEMENTATION` next to `vox4c::lib`, and passes when the build stops
with the explanatory `#error` of `vox4c.h`; it fails if the file compiles or fails for another reason. The target
is not part of the normal build and has nothing to run. Without `VOX4C_BUILD_LIBRARY` it sets `VOX4C_PREBUILT`
itself (the definition `vox4c::lib` gives its users), so the check is always available.

## Examples and tests

```
cc -std=c99 -O2 examples/vox4c_info.c -o vox4c_info   # prints layers, models, materials and the node tree
cc -std=c99 -O2 examples/vox2obj.c    -o vox2obj      # bakes a frame into OBJ + MTL
cc -std=c99 -O2 examples/vox4c_memory.c -o vox4c_memory # mesh memory: float vs packed vertices (Markdown table)
cc -std=c99 -O2 examples/vox4c_anim.c   -o vox4c_anim   # plays the animation: instances, meshes on demand, --obj baking
cc -std=c99 -O2 tests/vox4c_tests.c    -o vox4c_tests -lm
./vox4c_tests file1.vox file2.vox ...
```

The tests check, for every coordinate preset, winding, pivot and scale: mesh area equals exposed voxel faces, signed
volume equals voxel count, normals agree with winding, compacted and full materials give identical results, and that
single-frame loading equals sampling the full animation. Meshes built on demand must equal the ones built while
loading (float, packed, 16-bit, clockwise), instances must equal the per-node matrices, baked frames must enclose
exactly the drawn voxels, and smooth sampling must pass through every key; a synthetic file checks loops, the
timeline and interpolation between keys. They also fuzz the loader with truncated and corrupted data
and check every material key / type combination MagicaVoxel writes, and that packed vertices decode bit-exactly
to the float arrays (in every coordinate system, winding, pivot, scale and material mode).
Scene transforms were cross-checked frame by frame against `ogt_vox.h`.

## Limitations

- Monotone meshing produces T-junctions; they are watertight in exact arithmetic but can show hairline cracks with
  some rasterizers. Weld or use a different mesher if that matters.
- From the render settings only the background, sun, uniform sky light and lens are read (not the atmosphere,
  image based lighting, fog, film, bloom, ground, edges or bounce settings). `NOTE` (palette notes) and `IMAP`
  (palette display order) chunks are skipped; colours are correct, material indices follow the file order.
- Cameras and the sun were checked against MagicaVoxel renders: perspective and free camera poses and framing,
  the sun's direction (azimuth / elevation) and its apparent size (`area * 43.5` degrees, from `ogt_vox`, fits the
  penumbra of a render to about ±1.5°) and all three render lenses. Not tested: camera slots in the old `pano`
  camera mode (treated as the panorama lens).
- Model extents are clamped to 256 per axis (voxel coordinates are 8-bit in the format).

## Credits

- Monotone meshing: Mikola Lysenko, [Meshing in Minecraft, part 2](https://0fps.net/2012/07/07/meshing-minecraft-part-2/),
  and the `MonotoneMesh` implementation of mik14a's VoxImporter for Unreal Engine.
- Format references: [ephtracy/voxel-model](https://github.com/ephtracy/voxel-model/blob/master/MagicaVoxel-file-format-vox.txt),
  [Paul Bourke](https://paulbourke.net/dataformats/vox/), and
  [opengametools `ogt_vox.h`](https://github.com/jpaver/opengametools) by Justin Paver.

## License

MIT, see the end of `vox4c.h`.
