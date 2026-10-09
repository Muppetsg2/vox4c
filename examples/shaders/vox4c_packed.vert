#version 330 core
// Vertex shader for meshes loaded with V4C_FLAG_PACKED_VERTICES.
//
// One interleaved VBO of v4c_packed_vertex (8 bytes per vertex):
//
//   glVertexAttribPointer (0, 3, GL_UNSIGNED_SHORT, GL_FALSE, 8, (void*)0); // x, y, z -> vec3
//   glVertexAttribIPointer(1, 1, GL_UNSIGNED_BYTE,            8, (void*)6); // normal  -> uint
//   glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE,            8, (void*)7); // material-> uint
//
// Note the "I" in glVertexAttribIPointer for the integer attributes: with the
// plain glVertexAttribPointer they would arrive as floats and the `uint`
// inputs below would read garbage.
//
// The decoded values are bit-identical to the float arrays of an unpacked
// mesh (v4c_mesh::positions / normals / texcoords / materials).

layout(location = 0) in vec3 a_position;   // voxel-grid corner, 0..256
layout(location = 1) in uint a_normal;     // v4c_axis: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z
layout(location = 2) in uint a_material;   // index into v4c_scene::materials

uniform mat4  u_model;          // v4c_node_model_matrix(scene, node, frame, ...)
uniform mat4  u_view_projection;
uniform vec3  u_pivot;          // v4c_model::pivot
uniform float u_scale;          // v4c_scene::scale
uniform float u_num_materials;  // v4c_scene::num_materials

const vec3 AXIS[6] = vec3[6](vec3( 1.0,  0.0,  0.0), vec3(-1.0,  0.0,  0.0),
                             vec3( 0.0,  1.0,  0.0), vec3( 0.0, -1.0,  0.0),
                             vec3( 0.0,  0.0,  1.0), vec3( 0.0,  0.0, -1.0));

flat out vec3 v_normal;         // world space; flat: one normal per face anyway
flat out uint v_material;
out vec2      v_texcoord;       // same as v4c_mesh::texcoords
out vec3      v_world_position;

void main()
{
    // identical to v4c_mesh::positions of an unpacked mesh
    vec3 position = (a_position - u_pivot) * u_scale;

    // identical to v4c_mesh::normals; the rotation part of vox4c matrices is a
    // signed permutation (no shear / non-uniform scale), so mat3(u_model)
    // transforms normals correctly without an inverse-transpose
    v_normal   = mat3(u_model) * AXIS[a_normal];

    // identical to v4c_mesh::texcoords
    v_texcoord = vec2((float(a_material) + 0.5) / u_num_materials, 0.5);
    v_material = a_material;

    vec4 world       = u_model * vec4(position, 1.0);
    v_world_position = world.xyz;
    gl_Position      = u_view_projection * world;
}
