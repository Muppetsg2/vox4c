#version 330 core
// Reproduces MagicaVoxel's render lenses (perspective, stereographic,
// panorama) in any engine: render the scene into a cubemap from the camera
// position (v4c_camera_pose), then draw a fullscreen triangle with this shader.
//
// For the perspective lens a normal projection matrix with the vertical fov
// gives the same image directly; the cubemap path is needed for the other two.
//
// The direction math is identical to v4c_camera_ray() (checked against it and
// against MagicaVoxel renders).

uniform samplerCube u_scene;        // scene rendered around the camera position
uniform vec2        u_resolution;   // output size in pixels
uniform int         u_projection;   // v4c_projection: 0 perspective, 1 stereographic, 2 panorama
uniform float       u_fov;          // scene->render.fov, degrees (vertical)
// camera basis in the cubemap's coordinate system (from v4c_camera_pose:
// forward, up, and right = forward x up, negated for left-handed systems)
uniform vec3        u_forward;
uniform vec3        u_up;
uniform vec3        u_right;

out vec4 o_color;

const float PI = 3.14159265358979323846;

// Direction of the ray through `pixel` (pixel units, origin in the top-left
// corner; with gl_FragCoord flip y: pixel.y = size.y - gl_FragCoord.y).
// Returns false outside a panorama.
bool vox4c_lens_direction(vec2 pixel, vec2 size, int projection, float fov_deg, out vec3 dir)
{
    // vertical -1..1 over the height, horizontal scaled by the aspect ratio
    vec2 s = vec2(pixel.x - 0.5 * size.x, 0.5 * size.y - pixel.y) / (0.5 * size.y);

    if (projection == 2) // panorama: equirectangular, PI / height radians per pixel on both axes
    {                    // (360 x 180 degrees for a 2:1 image, 180 x 180 for a square one)
        float az = s.x * 0.5 * PI;
        float el = s.y * 0.5 * PI;
        if (abs(el) > 0.5 * PI)
            return false;
        dir = u_forward * (cos(el) * cos(az)) + u_right * (cos(el) * sin(az)) + u_up * sin(el);
        return true;
    }
    if (projection == 1) // stereographic: r = tan(theta / 2) / tan(fov / 4)
    {
        vec2  q  = s * tan(radians(fov_deg) * 0.25);
        float q2 = dot(q, q);
        dir = normalize(((1.0 - q2) * u_forward + 2.0 * q.x * u_right + 2.0 * q.y * u_up) / (1.0 + q2));
        return true;
    }
    // perspective (and unknown lenses): pinhole with the vertical fov
    dir = normalize(u_forward + (u_right * s.x + u_up * s.y) * tan(radians(fov_deg) * 0.5));
    return true;
}

void main()
{
    vec2 pixel = vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y);
    vec3 dir;
    if (!vox4c_lens_direction(pixel, u_resolution, u_projection, u_fov, dir))
    {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    o_color = texture(u_scene, dir);
}
