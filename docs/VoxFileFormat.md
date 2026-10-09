# MagicaVoxel .vox File Format

A description of the `.vox` format written by MagicaVoxel 0.99.x, following the layout of the original
specification by ephtracy ([MagicaVoxel-file-format-vox.txt](https://github.com/ephtracy/voxel-model/blob/master/MagicaVoxel-file-format-vox.txt)
and [MagicaVoxel-file-format-vox-extension.txt](https://github.com/ephtracy/voxel-model/blob/master/MagicaVoxel-file-format-vox-extension.txt)),
extended with what was found while implementing [vox4c](../README.md): the meaning of values the original files
do not document, default values, and the conventions of the renderer, cameras and lenses.

The document has two parts:

- **Part I** describes the current format, as written by **MagicaVoxel 0.99.7** (file version **200**). A reader
  that only needs files of the current editor can stop there.
- **Part II** lists what files of older versions (0.98.2 to 0.99.6.4) do differently and how MagicaVoxel 0.99.7
  converts them. It was established by saving the same model with every version and re-saving the old files
  with 0.99.7.

Where a statement goes beyond the original specification, its source is marked:

| mark | meaning |
|---|---|
| **[render]** | measured on images rendered by MagicaVoxel |
| **[ogt]** | behaviour of [ogt_vox.h](https://github.com/jpaver/opengametools), cross-checked frame by frame |
| **[observed]** | seen in files written by MagicaVoxel, meaning not confirmed |

---

# Part I. The current format (MagicaVoxel 0.99.7, version 200)

## 0. Conventions

- All numbers are **little endian**. `int32` is a signed 32 bit integer, `float` an IEEE 754 32 bit float.
- **Coordinate system:** right handed, **+Z up** ("gravity direction" in the original text). The front of a model
  faces **−Y**: the default camera looks along +Y at it, and +X is on its right.
- **Units:** one voxel = 1 unit. Model coordinates are integers 0..size−1.
- Most values of the scene, material and render chunks are stored as **text** inside dictionaries: numbers in
  decimal with `.` as separator, several numbers separated by a space (`"0 0 20"`), booleans as `"0"` / `"1"`,
  colours as `"r g b"` with 0..255 components.
- MagicaVoxel **omits keys whose value is the default**, so a missing key means "default", not "unknown".

### (a) STRING

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | buffer size (in bytes), N |
| N | char | string characters, not terminated by `'\0'` |

### (b) DICT

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | number of key-value pairs, N |
| | | for each pair: |
| | STRING | key |
| | STRING | value |

Keys start with `_`. A reader should ignore keys it does not know.

### (c) ROTATION

A rotation is stored as one byte (written as a decimal string in the `_r` key) that encodes a 3×3 matrix with a
single ±1 in every row and column: 48 possible rotations and reflections.

| bit | value in the example | meaning |
|---|---|---|
| 0–1 | 1 | column of the non-zero entry in the first row |
| 2–3 | 2 | column of the non-zero entry in the second row |
| 4 | 0 | sign in the first row (0: positive, 1: negative) |
| 5 | 1 | sign in the second row |
| 6 | 1 | sign in the third row |

The column of the third row is the remaining one (`3 − c0 − c1`). The example is the byte `105` and decodes to

```
 0  1  0
 0  0 -1
-1  0  0
```

The matrix multiplies column vectors: `p' = R · p`. The identity is `4` (`c0 = 0`, `c1 = 1`, all signs positive),
which is also the value to use when `_r` is missing. The determinant can be −1: MagicaVoxel stores mirrored
models this way. A reader must reject bytes where `c0` or `c1` is 3 or `c0 == c1` (vox4c falls back to identity).

---

## 1. File Structure : RIFF style

| # Bytes | Type | Value |
|---|---|---|
| 1×4 | char | id `'VOX '` : `'V' 'O' 'X' 'space'`, `'V'` is first |
| 4 | int32 | version number: **200** |
| | Chunk | `'MAIN'` |

```
Chunk 'MAIN'
{
    // models, in pairs; the pair index is the model id used by 'nSHP'
    Chunk 'SIZE'
    Chunk 'XYZI'
    ...

    // scene graph
    Chunk 'nTRN' / 'nGRP' / 'nSHP'   : any number, in any order
    Chunk 'LAYR'                     : layers

    // palette and materials
    Chunk 'RGBA'                     : palette
    Chunk 'MATL'                     : one per palette entry

    // editor and renderer
    Chunk 'rOBJ'                     : render settings, one per group of settings
    Chunk 'rCAM'                     : camera slots
    Chunk 'NOTE'                     : palette notes
    Chunk 'IMAP'                     : palette display order
    Chunk 'META'                     : file metadata [observed]
}
```

A reader must not depend on the order of the chunks after the models (for example `MATL` may come before or
after `RGBA`) and must skip chunks it does not know, using the sizes in the chunk header. All of these chunks are
children of `MAIN`; none of them has children of its own. Chunks that only older versions write (`PACK`, `MATT`)
are described in part II.

---

## 2. Chunk Structure

| # Bytes | Type | Value |
|---|---|---|
| 1×4 | char | chunk id |
| 4 | int32 | number of bytes of chunk content, N |
| 4 | int32 | number of bytes of children chunks, M |
| N | | chunk content |
| M | | children chunks |

Both sizes must be checked against the remaining file size; a content larger than what the reader parses is
allowed (skip to the end of it).

---

## 3. Chunk id 'MAIN' : the root chunk and parent of all other chunks

Content size 0. Its children size covers the rest of the file.

---

## 4. Chunk id 'SIZE' : model size

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | size x |
| 4 | int32 | size y |
| 4 | int32 | size z : gravity direction |

At most 256 per axis (voxel coordinates are single bytes).

---

## 5. Chunk id 'XYZI' : model voxels, paired with the preceding SIZE chunk

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | number of voxels, N |
| 4 × N | 4 × uint8 | (x, y, z, colour index) |

- The colour index is 1..255; 0 means empty and is not written by MagicaVoxel.
- Voxels are not sorted and a reader should not assume that coordinates are unique or inside the model size.
- All voxels of a model are stored, including the ones hidden inside the volume.

---

## 6. Chunk id 'RGBA' : palette

| # Bytes | Type | Value |
|---|---|---|
| 4 × 256 | 4 × uint8 | (R, G, B, A), for each palette entry |

The entries are **shifted by one**: entry *i* of the chunk (0-based) is the colour of colour index *i + 1*.

```
colour of index 1   = entry 0
colour of index 255 = entry 254
entry 255           : not used (index 0 is "empty")
```

MagicaVoxel 0.99.7 always writes this chunk. Without it the default palette applies (appendix A).

---

## 7. Scene graph

The scene is described with three node types, all identified by an `int32` node id that is unique in the
file:

```
          nTRN (0)            root: always the transform with id 0
            |
          nGRP (1)
         /   |   \
    nTRN   nTRN   nTRN        a transform has exactly one child
      |      |      |
    nSHP   nGRP   nSHP        a group has any number of transforms as children
             |                a shape references models
            ...
```

- Every `nGRP` or `nSHP` sits below an `nTRN`, which gives it its transform, name, visibility and layer.
- The same `nSHP` (or `nGRP`) can be the child of several transforms: this is how MagicaVoxel instances a model.
  The graph is therefore a DAG; a reader that builds a tree has to copy the shared part. Cycles do not occur in
  valid files but a reader should detect them.
- MagicaVoxel 0.99.7 always writes a scene graph. Files without one are older (part II, L.2).

**Placing a model.** The world matrix of a node is the product of the transforms from the root down,
`W = T(t_root) · R_root · … · T(t_node) · R_node`. A model is centred on its **pivot** `floor(size / 2)`: the voxel
with model coordinates *p* occupies the unit cube whose minimum corner is

```
world = W · (p − floor(size / 2))
```

so for an even size the model's centre lands exactly on the translation and for an odd size half a voxel beside
it. **[ogt]**

---

## 8. Chunk id 'nTRN' : transform node

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | node id |
| | DICT | node attributes |
| 4 | int32 | child node id |
| 4 | int32 | reserved id (must be −1) |
| 4 | int32 | layer id, −1 for none |
| 4 | int32 | number of frames, N (at least 1) |
| | DICT × N | frame attributes |

Node attributes:

| key | value |
|---|---|
| `_name` | STRING |
| `_hidden` | 0 / 1 |
| `_loop` | 0 / 1: loop the transform animation **[ogt]** |
| `_frm_t` | three integers, e.g. `"29 0 20"` **[observed]**, meaning unknown |

Frame attributes:

| key | value |
|---|---|
| `_r` | ROTATION, as a decimal byte; identity when missing |
| `_t` | translation, three integers `"x y z"`; zero when missing |
| `_f` | frame index of this key, from 0; 0 when missing |

### Animation **[ogt]**

The frames are keyframes, sorted by `_f`. The value at frame *f*:

1. With `_loop` set, *f* is first wrapped into the key range `[first, last]` of this node:
   `first + (f − first) mod (last − first + 1)` (and mirrored accordingly for *f* < first).
2. Before the first key the first key applies, after the last key the last one.
3. Between two keys the **rotation** is the one of the earlier key (no interpolation) and the **translation** is
   interpolated linearly and **truncated toward zero** to whole voxels.

The animation of the whole file has `max(_f) + 1` frames; `META` may store the range shown in the editor.

---

## 9. Chunk id 'nGRP' : group node

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | node id |
| | DICT | node attributes (none seen) |
| 4 | int32 | number of children, N |
| 4 × N | int32 | child node ids (transforms) |

---

## 10. Chunk id 'nSHP' : shape node

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | node id |
| | DICT | node attributes |
| 4 | int32 | number of models, N (at least 1) |
| | | for each model: |
| 4 | int32 | model id (index of the SIZE / XYZI pair) |
| | DICT | model attributes |

| attribute | of | value |
|---|---|---|
| `_loop` | node | 0 / 1: loop the model animation **[ogt]** |
| `_frm_id` | node | **[observed]**, meaning unknown |
| `_f` | model | frame index from which this model is shown |

The models are keyframes like the transforms of `nTRN`: at frame *f* the model of the last key with `_f ≤ f` is
shown (no interpolation, the same looping rules). A shape with one model shows it in every frame.

---

## 11. Chunk id 'MATL' : material

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | material id: the palette index it applies to |
| | DICT | material properties |

MagicaVoxel 0.99.7 writes 256 chunks with the ids 1..256; id 256 is the unused colour index 0 (`id & 0xFF`). It
ignores the id 0 that 0.99.1 writes (part II, L.4). Every key can appear with every type; the last column tells which types MagicaVoxel
uses it for. A key is left out when it has the value of the "default" column (so a metal that was never edited
has no `_metal` key); `_rough`, `_ior` and `_ri` are always written. This was established from files containing
every combination of type and medium that the editor offers.

`_d` is the exception: it is left out when it is **0**, while a new palette starts with 0.05 and writes it. A
material without a `MATL` chunk therefore has 0.05, but a `MATL` without `_d` has 0 (seen in files whose
materials came from older versions, and in a converted glass with `_att` 0). **[converted]**

| key | value | default | used by |
|---|---|---|---|
| `_type` | `_diffuse`, `_metal`, `_glass`, `_emit`, `_blend`, `_media` | diffuse (also when missing) | |
| `_rough` | roughness | 0.1 | all surfaces |
| `_metal` | metalness | 0 | metal, blend |
| `_sp` | specular, 0..2 | 1 | metal, blend |
| `_ior` | index of refraction **minus 1** | 0.3 | glass, blend |
| `_ri` | index of refraction (always `_ior + 1`) | 1.3 | glass, blend |
| `_trans` | transparency; `_alpha` holds the same value | 0 | glass, blend |
| `_emit` | emission strength | 0 | emit |
| `_flux` | emission power exponent | 0 | emit |
| `_ldr` | low dynamic range emission | 0 | emit |
| `_media_type` | `_scatter`, `_emit`, `_sss`; missing = absorb | absorb | glass, blend, media |
| `_media` | the same as a number: 0 absorb, 1 scatter, 2 emit, 3 sss (written together with `_media_type`) | 0 | glass, blend, media |
| `_d` | density of the medium, 0..0.1 | 0 when left out (a new palette: 0.05) | glass, blend, media |
| `_g` | phase: scattering anisotropy (Henyey–Greenstein g) | 0 | glass, blend, media |

Material types:

| `_type` | |
|---|---|
| `_diffuse` | matte surface |
| `_metal` | metal |
| `_glass` | transparent, refracting surface; may contain a medium |
| `_emit` | light source |
| `_blend` | a mix of diffuse, metal and glass parameters |
| `_media` | a participating medium (cloud, smoke) without a surface |

---

## 12. Chunk id 'LAYR' : layer

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | layer id |
| | DICT | layer attributes |
| 4 | int32 | reserved id (must be −1) |

| key | value |
|---|---|
| `_name` | STRING |
| `_hidden` | 0 / 1: hides every node in this layer |
| `_color` | `"r g b"`, the layer colour in the editor |

MagicaVoxel 0.99.7 writes 32 layers (16 when it re-saves a file of an older version): 0..7 named `"0"`..`"7"`
(or without a name) with their own colours, the others without a name and with the colour `150 150 150`.

A node is visible when neither it, nor any of its ancestors, nor the layers of any of them is hidden.

---

## 13. Chunk id 'rOBJ' : render settings

| # Bytes | Type | Value |
|---|---|---|
| | DICT | settings; the key `_type` tells which group |

MagicaVoxel 0.99.7 writes the 15 groups below, one chunk each. The groups marked **[render]** were measured; for
the others the table lists the keys found in files and the meaning suggested by their names and the editor.

| `_type` | keys | meaning |
|---|---|---|
| `_inf` | `_i`, `_k`, `_angle`, `_area`, `_disk` | the sun, see 13.1 **[render]** |
| `_uni` | `_i`, `_k` | uniform sky light: intensity, colour `"r g b"` |
| `_ibl` | `_path`, `_i`, `_angle`, `_rot` | image based lighting: HDR file name, intensity, rotation |
| `_atm` | `_ray_d`, `_ray_k`, `_mie_d`, `_mie_k`, `_mie_g`, `_o3_d`, `_o3_k` | atmosphere (Rayleigh, Mie, ozone) |
| `_env` | `_mode` | which environment light is used **[observed]**, values not mapped |
| `_fog_uni` | `_d`, `_k`, `_g` | fog: density, colour, phase |
| `_lens` | `_proj`, `_fov`, `_aperture`, `_blade_n`, `_blade_r` | camera lens, see 13.2 **[render]** |
| `_film` | `_expo`, `_vig`, `_aces`, `_gam` | exposure, vignette, ACES tone mapping, gamma |
| `_bloom` | `_mix`, `_scale`, `_aspect`, `_threshold` | bloom |
| `_ground` | `_color`, `_hor` | ground plane |
| `_bg` | `_color` | background colour of the editor; the render shows it when `_setting` `_bg_c` is 1 |
| `_edge` | `_color`, `_width` | edge outlines |
| `_grid` | `_color`, `_spacing`, `_width`, `_display` | grid |
| `_bounce` | `_diffuse`, `_specular`, `_scatter`, `_energy` | path tracer bounces |
| `_setting` | `_ground`, `_grid`, `_edge`, `_bg_c`, `_bg_a`, `_aa`, `_cell`, `_scale`, `_sw` | switches of the render tab: `_bg_c` 1 shows the background colour in the render instead of the sky (checked by toggling it in the editor); the others **[observed]** |

### 13.1 The sun (`_type` = `_inf`) **[render]**

| key | value |
|---|---|
| `_i` | intensity |
| `_k` | colour `"r g b"` |
| `_angle` | `"elevation azimuth"` in degrees |
| `_area` | size of the sun |
| `_disk` | 0 / 1: sun disk visible in the sky |

- **Elevation** is the angle above the horizon. **Azimuth** 0 puts the sun in front of the model (−Y), 90 to its
  right (+X), 180 behind, 270 left. The direction **towards** the sun is

  ```
  ( cos(el) sin(az),  −cos(el) cos(az),  sin(el) )
  ```

  and the light travels the opposite way.
- `_area` = 0 gives a point-like sun (sharp shadows, a directional light). Otherwise the sun is a disk with an
  apparent diameter of about **`_area × 43.5°`** (the constant comes from ogt_vox; the penumbra in a render fits
  it within about ±1.5°).

### 13.2 The lens (`_type` = `_lens`) **[render]**

| key | value |
|---|---|
| `_proj` | projection: **0** perspective, **1** stereographic, **2** panorama |
| `_fov` | field of view in degrees, **vertical** |
| `_aperture` | depth of field |
| `_blade_n`, `_blade_r` | aperture blades: count, rotation |

The fov of the render is this `_fov`; the fov stored in a camera slot (`rCAM`) is not necessarily the same.

For a W × H image let a pixel position (x, y) (origin top left, pixel centres at +0.5) be expressed relative to
half the height:

```
sx = (x − W/2) / (H/2)        horizontal, scaled by the aspect ratio
sy = (H/2 − y) / (H/2)        vertical, −1..1
```

With the camera basis F (forward), U (up) and R = F × U (right), all unit vectors, the ray direction is:

| projection | direction |
|---|---|
| perspective | `F + (R·sx + U·sy) · tan(fov/2)` |
| stereographic | with `q = (sx, sy) · tan(fov/4)`: `((1 − |q|²) F + 2 q.x R + 2 q.y U) / (1 + |q|²)` |
| panorama | with `az = sx · 90°`, `el = sy · 90°`: `F cos(el) cos(az) + R cos(el) sin(az) + U sin(el)` |

- **Stereographic:** a direction at angle θ from F lands `tan(θ/2) / tan(fov/4)` half-heights from the image
  centre. Nearly the whole sphere is visible.
- **Panorama:** equirectangular; the height always covers 180° and the horizontal scale is the same (π / H radians
  per pixel): a 2:1 image covers 360°, a square image 180°, and wider images repeat the directions every 2H pixels.
  `_fov` has no effect.

---

## 14. Chunk id 'rCAM' : camera slot

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | camera id, the slot 0..9 |
| | DICT | camera attributes |

MagicaVoxel writes its 10 camera slots. A slot into which no view was saved holds the defaults
(focus `0 0 0`, angle `0 0 0`, radius 0). **The live view of the editor is not stored in the file**, only these
slots.

| key | value | default |
|---|---|---|
| `_mode` | `pers`, `free`, `orth`, `iso` (`pano` in ogt_vox) | pers |
| `_focus` | `"x y z"`, the point the camera orbits and looks at | 0 0 0 |
| `_angle` | `"pitch yaw third"` in degrees | 0 0 0 |
| `_radius` | distance from the focus to the camera | 0 |
| `_frustum` | perspective / free: `tan(fov/2)`; orthographic / isometric: half the view height at the focus | 0.414214 |
| `_fov` | vertical field of view in degrees at the time the slot was saved | 45 |

### Camera pose **[render]**

- **Angles 0 0 0** look along **+Y** with +Z up (at the front of the model).
- **Pitch:** negative looks down (−45 sees the top and the front of a cube; isometric slots store −35.2644).
- **Yaw:** positive orbits the camera to the right around +Z (90 puts it at +X, looking at the +X side).
- **The third value is not a roll**: isometric slots store e.g. 34 without any tilt of the image.

```
forward  = ( −cos(pitch) sin(yaw),  cos(pitch) cos(yaw),  sin(pitch) )
up       = (  sin(pitch) sin(yaw), −sin(pitch) cos(yaw),  cos(pitch) )
position = focus − forward · radius
```

This holds for `pers` and `free` slots alike. For `orth` and `iso` the rays are parallel to `forward`, the
view spans `±_frustum` vertically around the focus and `radius` is only a large distance that keeps the camera
outside the scene.

---

## 15. Chunk id 'NOTE' : palette notes

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | number of names, N |
| | STRING × N | names |

MagicaVoxel writes 32 names, presumably one for each row of 8 colours of the palette window; most are empty.
**[observed]**

---

## 16. Chunk id 'IMAP' : palette display order

| # Bytes | Type | Value |
|---|---|---|
| 256 | uint8 | for each position in the palette window, the palette index shown there |

The original text lists the entries as `int32`, but the chunk size is 256 bytes. Reordering colours in
MagicaVoxel's palette window writes this map instead of changing the voxel data: colours and voxels stay correct
without it; it only tells in which order the editor shows them. **[ogt]**

---

## 17. Chunk id 'META' : metadata **[observed]**

| # Bytes | Type | Value |
|---|---|---|
| | DICT | metadata |

| key | value |
|---|---|
| `_anim_range` | `"first last"`, the animation range of the editor's timeline (e.g. `"0 30"`) |

---

## 18. Notes for readers

- Check every size against the remaining bytes; skip unknown chunks by their sizes.
- Ignore unknown dictionary keys; treat a missing key as its default.
- Clamp voxel coordinates to the model size and colour indices to 1..255.
- When the same chunk appears twice for one id (`MATL`, `rCAM`), the later one wins.
- A scene graph may reference missing nodes or models; skip those references.
- Limit the depth and the total size when expanding shared nodes: a small file can describe a very large tree.
- Do not decide anything from the version number in the header (part II, L.1): look at which chunks and keys
  are present.

---

# Part II. Older files

Everything in part I applies to older files as well, except for the differences below. They were established by
saving the same model (`chr_knight`) with MagicaVoxel 0.98.2, 0.99.1, 0.99.4.2, 0.99.6.4 and 0.99.7, by opening the
files of 0.98.2, 0.99.1 and 0.99.4.2 in 0.99.7 and saving them again (which shows how 0.99.7 itself converts
them), with three `PACK` animations of 0.98.2 (one with frames of different sizes), and with the files of the
original model collection, which predate 0.98.2. The versions 0.99.2, 0.99.3 and 0.99.5 are no longer available
and were not examined: "0.99.1 – 0.99.4" means "seen in both, assumed in between".

Marks used in this part, in addition to those of part I:

| mark | meaning |
|---|---|
| **[converted]** | what MagicaVoxel 0.99.7 writes when it re-saves the older file |

---

## L.1 The version number

| MagicaVoxel | version in the header |
|---|---|
| before 0.98.2 | 150 |
| 0.98.2 | 150 |
| 0.99.1 | 150 |
| 0.99.4.2 | 150 |
| 0.99.6.4 | 150 |
| 0.99.7 | **200** |

The number only changed with 0.99.7, although the format changed a lot before that. A reader must decide by the
chunks and keys it finds, never by the version number. What each version writes:

| | before 0.98.2 | 0.98.2 | 0.99.1 | 0.99.4.2 | 0.99.6.4 | 0.99.7 |
|---|---|---|---|---|---|---|
| `PACK` | yes | yes | – | – | – | – |
| `SIZE` / `XYZI` | yes | yes | yes | yes | yes | yes |
| scene graph (`nTRN` …) | – | – | yes | yes | yes | yes |
| `LAYR` | – | – | 8, name only | 8, name only | 32, name and colour | 32 (16 when re-saving an older file) |
| `RGBA` | only for a custom palette | yes | yes | yes | yes | yes |
| `MATT` | – | edited materials only | – | – | – | – |
| `MATL` | – | – | 256, ids 0..255 | 256, ids 1..256 | 256, ids 1..256 | 256, ids 1..256 |
| `rOBJ` | – | – | 12 groups (L.6) | 14 groups (L.6) | 15 groups | 15 groups |
| `rCAM` | – | – | – | – | 10 slots | 10 slots |
| `NOTE` | – | – | – | – | yes | yes |
| `META` | – | – | – | – | – | `_anim_range` |

The bytes of `SIZE`, `XYZI` and the used `RGBA` entries are identical in all versions and survive every
conversion unchanged. The unused last `RGBA` entry (colour index 0) is `0 0 0 255` in files before 0.98.2 and
`0 0 0 0` afterwards.

---

## L.2 Files without a scene graph (0.98.2 and older)

### Chunk id 'PACK' : number of models

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | number of SIZE / XYZI pairs |

It comes first in `MAIN`. With more than one model the models are the frames of an animation: model *i* is
frame *i*. The frames may have different sizes (`horse`: 31×7×24, 32×7×24, 31×7×25, 31×8×24).

### Conversion **[converted]**

0.99.7 turns such a file into

```
nTRN 0 → nGRP 1 → nTRN 2 (_t = "0 0 floor(size.z / 2)", layer 0) → nSHP 3
```

so the model stands on the ground, its lowest voxel layer at z = 0. With one model, `nSHP` holds it without
attributes. With a `PACK` animation of *N* models, `nSHP` gets the node attribute `_frm_id 0` and the models
0..N−1 with `_f` = 0..N−1, without `_loop`: one model per frame, the last one held after the end. Every frame
keeps its own size and is centred on its own pivot (part I, 7); the translation uses the height of frame 0
(the files examined do not tell frame 0 from the tallest frame: 24 and 25 voxels both give 12).

### Palette

Files written before 0.98.2 contain an `RGBA` chunk only when the palette was changed; without it the default
palette (appendix A) applies. 0.98.2 always writes it.

---

## L.3 Chunk id 'MATT' : material (0.98.2)

0.98.2 writes one chunk for each material that was changed in the editor; the others are diffuse with default
values. A `MATL` for the same index takes precedence.

| # Bytes | Type | Value |
|---|---|---|
| 4 | int32 | id: the colour index 1..255 |
| 4 | int32 | type: 0 diffuse, 1 metal, 2 glass, 3 emissive |
| 4 | float | weight: diffuse 1.0; metal, glass, emissive (0.0, 1.0] |
| 4 | int32 | property bits |
| 4 × N | float | one value for each set bit **0..6**, in bit order |

| bit | property | value |
|---|---|---|
| 0 | plastic | 0.0 / 1.0 |
| 1 | roughness | 0..1 |
| 2 | specular | 0..1, default 0.5 |
| 3 | IOR | `n − 1`, like `_ior` (e.g. 0.505 for n = 1.505) **[observed]** |
| 4 | attenuation | 0..1 |
| 5 | power | e.g. 2 |
| 6 | glow | 0..1 |
| 7 | total power | **no value**: a flag only (a chunk with bits `0xef` has 6 floats) **[observed]** |

The properties are those of the `MATL` keys of 0.99.1 (`_plastic`, `_rough`, `_spec`, `_ior`, `_att`, `_flux`,
`_glow`, `_unit`) and the weight is `_weight`.

**0.99.7 ignores `MATT` [converted]:** re-saving a 0.98.2 file with a glass, a metal and two emissive materials
gives 256 default diffuse materials. vox4c still reads `MATT` and converts it by the rules of L.4 (L.8).

---

## L.4 Chunk id 'MATL' in 0.99.1 – 0.99.4

**Ids.** 0.99.1 numbers the 256 chunks **0..255**, every version from 0.99.4 on **1..256**. In both the id is the
colour index (0.99.1 writes its non-default materials under the ids of the used colours). 0.99.7 ignores the id 0
of a 0.99.1 file (the unused index 0 then gets the defaults of a new palette) and reads the ids 1..256 as usual.

**Keys.** Every chunk carries all keys of its version, edited or not (a reader can recognise these chunks by
`_weight`, which no later version writes):

| version | keys written |
|---|---|
| 0.99.1 | `_type`, `_weight`, `_rough`, `_spec`, `_ior`; `_plastic` (diffuse); `_flux`, `_glow`, `_unit` (emit); `_att` (glass) |
| 0.99.4.2 | `_type`, `_weight`, `_rough`, `_spec`, `_spec_p`, `_ior`, `_att`, `_g0`, `_g1`, `_gw`, `_flux`, `_ldr` |
| 0.99.6.4 | the current keys (part I, 11), but `_media` without `_media_type` and `_ior` without `_ri` |

**Types.** 0.99.1 has `_diffuse`, `_metal`, `_glass`, `_emit`; plastic is a switch of the diffuse type
(`_plastic 1`). 0.99.4 adds `_plastic` and `_media` as types of their own. `_blend` appears with 0.99.6.

**Conversion [converted].** Comparing each material of the 0.99.1 and 0.99.4.2 files with the same material after
0.99.7 re-saved them gives these rules; `_rough` and `_ior` are kept as they are:

| old material | becomes |
|---|---|
| `_diffuse` | `_diffuse`; `_spec` is dropped |
| `_diffuse` with `_plastic 1` (0.99.1) | a plain `_diffuse`: `_plastic` and `_spec` are dropped |
| `_metal` | `_metal` with `_metal` = `_weight` and **`_sp` = `_spec` + 1** (0.642105 → 1.6421, 0.63 → 1.63) |
| `_glass` | `_glass` with `_trans` = `_alpha` = `_weight` and **`_d` from `_att`** (below); `_spec` is dropped |
| `_emit` | `_emit` with `_emit` = `_weight`; `_flux` and `_ldr` kept; **`_glow` and `_unit` are dropped** |
| `_plastic` (0.99.4) | **`_metal` without `_metal`** (metalness 0) and **`_ior` = `_spec_p`** (0.7 → `_ior 0.7`, `_ri 1.7`); `_weight` and `_spec` are dropped |
| `_media` (0.99.4) | `_media` with `_media_type _scatter` and **`_d` from `_weight`** (below) |
| `_g0`, `_g1`, `_gw` (0.99.4, all types) | **`_g` = `_gw` · `_g1` + (1 − `_gw`) · `_g0`** (defaults −0.5, 0.8, 0.7 give 0.41; −0.25, 0.9, 0.53 give 0.3595) |

**Density.** The `_d` of a converted medium follows from its `_weight`, that of a converted glass from its `_att`,
both as **`_d = −ln(1 − 0.4 · x)`, capped at 0.1** (the end of the density slider of 0.99.7). Exact to the printed
digits for every value examined:

| x | 0 | 0.01 | 0.05 | 0.1 | 0.2 | 0.22 | 0.25 … 1 |
|---|---|---|---|---|---|---|---|
| medium (`_weight`) | – | 0.00400803 | 0.0202027 | 0.040822 | 0.0833816 | 0.0921153 | 0.1 (0.25, 0.3, 0.33, 0.5, 1) |
| glass (`_att`) | 0 (no `_d`) | | 0.0202027 | 0.040822 | 0.0833816 | | 0.1 (0.34, 0.41, 0.5, 1) |

Read as a transmittance `1 − 0.4 · x`, this is the matching optical density. The cap is reached at x ≈ 0.238. A
medium with `_weight` 0 cannot be saved by 0.99.4 (it becomes diffuse); vox4c returns 0 for it. `_att` of the other
types is dropped.

**0.99.6.4.** Writes `_media` (a number, part I, 11) instead of `_media_type`, and `_ior` without `_ri`.

---

## L.5 Chunk id 'LAYR' in 0.99.1 – 0.99.4

8 layers (ids 0..7) with only a `_name` (`"0"`..`"7"`), no `_color`. The coloured layers of part I, 12 appear
with 0.99.6.

---

## L.6 Chunk id 'rOBJ' in 0.99.1 – 0.99.4

**0.99.1** writes 12 groups, several of them under names that were replaced later:

| `_type` | keys | in 0.99.7 **[converted]** |
|---|---|---|
| `_inf` | `_i`, `_k`, `_angle`, `_area` (no `_disk`) | kept, `_disk 0` added |
| `_uni` | `_i`, `_k` | kept |
| `_rayleigh` | `_d`, `_k` | dropped: `_atm` gets its defaults |
| `_mie` | `_d`, `_k`, `_g` | dropped: `_atm` gets its defaults |
| `_fog` | `_d`, `_k`, `_enable` | dropped: `_fog_uni` gets its defaults (density 0) |
| `_len` | `_fov`, `_dof`, `_exp`, `_vig`, `_sg`, `_gam`, `_blade_n`, `_blade_r` | dropped: `_lens` and `_film` get their defaults |
| `_bloom` | `_mix`, `_scale`, `_aspect`, `_threshold` | kept |
| `_aces` | (none) | dropped |
| `_ground` | `_color`, `_enable` | colour kept |
| `_bg` | `_color`, `_enable` | colour kept; `_enable 0` gives `_setting` `_bg_c 0` (also its default) |
| `_edge` | `_color`, `_width`, `_enable` | kept |
| `_grid` | `_color`, `_spacing`, `_width`, `_display`, `_enable` | kept |

So 0.99.7 keeps only the groups whose name did not change and resets the others; it converts no render setting.

- In `_len`, `_sg` = 1 selects the stereographic projection (`_proj` 1); a panorama projection did not exist yet.
- `_bg` `_enable` is the switch that later became `_setting` `_bg_c`: 1 shows the background colour in the
  render.

**0.99.4.2** already writes the current groups (part I, 13) except `_bounce`, and `_ibl` without `_path`; 0.99.7
keeps them all. **0.99.6.4** writes `_ibl` with `_rot`, 0.99.7 with `_angle` (two values).

---

## L.7 Chunks 'rCAM' and 'NOTE'

Written from 0.99.6 on; files of 0.99.4 and older have no camera slots and no palette notes. 0.99.7 writes ten
default slots when it re-saves them.

---

## L.8 What vox4c does with older files

vox4c returns older files the way MagicaVoxel 0.99.7 shows them after opening them: for the 0.99.1 and 0.99.4.2
files and the three animations above, the scene, the frames, the palette, all 256 materials and the background
switch are identical to those of their 0.99.7 re-saves. Where 0.99.7 throws information away entirely (`MATT`,
`_len`), vox4c keeps it:

| difference | handling |
|---|---|
| no scene graph, `PACK` | the graph of L.2 |
| no `RGBA` | default palette |
| `MATT` | **read** and converted by the rules of L.4 (unless a `MATL` exists for the index), unlike 0.99.7, which ignores it |
| `MATL` id 0 (0.99.1) | ignored, like 0.99.7 |
| `MATL` with `_weight` | converted by the rules of L.4; the raw `_weight`, `_att` and plastic switch stay in `weight`, `attenuation` and `plastic` |
| `_len` | **read** (lens: `_fov`, `_dof` as aperture, `_sg` as stereographic), unlike 0.99.7, which resets the lens |
| `_bg _enable` | `show_background`, like `_setting _bg_c` |
| `_rayleigh`, `_mie`, `_fog`, `_aces`, film keys | not read (like their current counterparts) |
| `LAYR` without `_color` | white |

---

## Appendix A. Default palette

Used when a file has no `RGBA` chunk. Each value is `0xAABBGGRR`, indexed by colour index (entry 0 is the unused
index 0, so this table is **not** shifted like the `RGBA` chunk).

<details>
<summary>256 entries</summary>

```
0x00000000, 0xffffffff, 0xffccffff, 0xff99ffff, 0xff66ffff, 0xff33ffff, 0xff00ffff, 0xffffccff,
0xffccccff, 0xff99ccff, 0xff66ccff, 0xff33ccff, 0xff00ccff, 0xffff99ff, 0xffcc99ff, 0xff9999ff,
0xff6699ff, 0xff3399ff, 0xff0099ff, 0xffff66ff, 0xffcc66ff, 0xff9966ff, 0xff6666ff, 0xff3366ff,
0xff0066ff, 0xffff33ff, 0xffcc33ff, 0xff9933ff, 0xff6633ff, 0xff3333ff, 0xff0033ff, 0xffff00ff,
0xffcc00ff, 0xff9900ff, 0xff6600ff, 0xff3300ff, 0xff0000ff, 0xffffffcc, 0xffccffcc, 0xff99ffcc,
0xff66ffcc, 0xff33ffcc, 0xff00ffcc, 0xffffcccc, 0xffcccccc, 0xff99cccc, 0xff66cccc, 0xff33cccc,
0xff00cccc, 0xffff99cc, 0xffcc99cc, 0xff9999cc, 0xff6699cc, 0xff3399cc, 0xff0099cc, 0xffff66cc,
0xffcc66cc, 0xff9966cc, 0xff6666cc, 0xff3366cc, 0xff0066cc, 0xffff33cc, 0xffcc33cc, 0xff9933cc,
0xff6633cc, 0xff3333cc, 0xff0033cc, 0xffff00cc, 0xffcc00cc, 0xff9900cc, 0xff6600cc, 0xff3300cc,
0xff0000cc, 0xffffff99, 0xffccff99, 0xff99ff99, 0xff66ff99, 0xff33ff99, 0xff00ff99, 0xffffcc99,
0xffcccc99, 0xff99cc99, 0xff66cc99, 0xff33cc99, 0xff00cc99, 0xffff9999, 0xffcc9999, 0xff999999,
0xff669999, 0xff339999, 0xff009999, 0xffff6699, 0xffcc6699, 0xff996699, 0xff666699, 0xff336699,
0xff006699, 0xffff3399, 0xffcc3399, 0xff993399, 0xff663399, 0xff333399, 0xff003399, 0xffff0099,
0xffcc0099, 0xff990099, 0xff660099, 0xff330099, 0xff000099, 0xffffff66, 0xffccff66, 0xff99ff66,
0xff66ff66, 0xff33ff66, 0xff00ff66, 0xffffcc66, 0xffcccc66, 0xff99cc66, 0xff66cc66, 0xff33cc66,
0xff00cc66, 0xffff9966, 0xffcc9966, 0xff999966, 0xff669966, 0xff339966, 0xff009966, 0xffff6666,
0xffcc6666, 0xff996666, 0xff666666, 0xff336666, 0xff006666, 0xffff3366, 0xffcc3366, 0xff993366,
0xff663366, 0xff333366, 0xff003366, 0xffff0066, 0xffcc0066, 0xff990066, 0xff660066, 0xff330066,
0xff000066, 0xffffff33, 0xffccff33, 0xff99ff33, 0xff66ff33, 0xff33ff33, 0xff00ff33, 0xffffcc33,
0xffcccc33, 0xff99cc33, 0xff66cc33, 0xff33cc33, 0xff00cc33, 0xffff9933, 0xffcc9933, 0xff999933,
0xff669933, 0xff339933, 0xff009933, 0xffff6633, 0xffcc6633, 0xff996633, 0xff666633, 0xff336633,
0xff006633, 0xffff3333, 0xffcc3333, 0xff993333, 0xff663333, 0xff333333, 0xff003333, 0xffff0033,
0xffcc0033, 0xff990033, 0xff660033, 0xff330033, 0xff000033, 0xffffff00, 0xffccff00, 0xff99ff00,
0xff66ff00, 0xff33ff00, 0xff00ff00, 0xffffcc00, 0xffcccc00, 0xff99cc00, 0xff66cc00, 0xff33cc00,
0xff00cc00, 0xffff9900, 0xffcc9900, 0xff999900, 0xff669900, 0xff339900, 0xff009900, 0xffff6600,
0xffcc6600, 0xff996600, 0xff666600, 0xff336600, 0xff006600, 0xffff3300, 0xffcc3300, 0xff993300,
0xff663300, 0xff333300, 0xff003300, 0xffff0000, 0xffcc0000, 0xff990000, 0xff660000, 0xff330000,
0xff0000ee, 0xff0000dd, 0xff0000bb, 0xff0000aa, 0xff000088, 0xff000077, 0xff000055, 0xff000044,
0xff000022, 0xff000011, 0xff00ee00, 0xff00dd00, 0xff00bb00, 0xff00aa00, 0xff008800, 0xff007700,
0xff005500, 0xff004400, 0xff002200, 0xff001100, 0xffee0000, 0xffdd0000, 0xffbb0000, 0xffaa0000,
0xff880000, 0xff770000, 0xff550000, 0xff440000, 0xff220000, 0xff110000, 0xffeeeeee, 0xffdddddd,
0xffbbbbbb, 0xffaaaaaa, 0xff888888, 0xff777777, 0xff555555, 0xff444444, 0xff222222, 0xff111111
```

</details>