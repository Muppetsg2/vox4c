#version 330 core
// Minimal fragment shader for vox4c_packed.vert: palette colour + Lambert.
//
// u_palette is the texture written by v4c_palette_texture():
// width = num_materials, height = 1, RGBA8, GL_NEAREST filtering.

flat in vec3 v_normal;
flat in uint v_material;
in vec2      v_texcoord;
in vec3      v_world_position;

uniform sampler2D u_palette;
uniform vec3      u_light_direction = vec3(0.3, 0.8, 0.5); // towards the light, world space

out vec4 o_color;

void main()
{
    // Either lookup works: texelFetch with the material index is exact,
    // texture() with v_texcoord matches what an unpacked mesh would use.
    vec4 albedo = texelFetch(u_palette, ivec2(int(v_material), 0), 0);
    // vec4 albedo = texture(u_palette, v_texcoord);

    float lambert = max(dot(normalize(v_normal), normalize(u_light_direction)), 0.0);
    o_color = vec4(albedo.rgb * (0.25 + 0.75 * lambert), albedo.a);
}
