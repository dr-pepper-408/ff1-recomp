-- The 3D view's shaders and vertex format.
local ffi = require("ffi")
require("ff1core")                         -- (Ff1Vertex)
local Sh = {}

Sh.VERTEX_SIZE = ffi.sizeof("Ff1Vertex")
Sh.VERTEX_FORMAT = {
    { "VertexPosition", "float", 3 },
    { "VertexTexCoord", "float", 2 },
    { "VertexColor", "byte", 4 },
}

-- The scene: the world, the cards and the battle stage.
Sh.SCENE = [[
uniform mat4 proj;
uniform mat4 view;
uniform vec3 offset;
uniform vec3 focus;
uniform vec3 sky;
uniform float view_dist;
uniform vec3 tint;
uniform float mono;
uniform float time;
uniform float water;
uniform vec3 daylight;
uniform float outline;     // > 0: cutouts get a dark rim (this many texels of atlas: 1 / size)
varying vec3 wpos;
#ifdef VERTEX
vec4 position(mat4 transform_projection, vec4 vertex_position) {
    wpos = vertex_position.xyz + offset;
    return proj * view * vec4(wpos, 1.0);
}
#endif
#ifdef PIXEL
vec4 effect(vec4 color, Image tex, vec2 uv, vec2 screen_coords) {
    vec4 c = Texel(tex, uv) * color;
    if (c.a < 0.5) {
        // A dark rim round the cutouts: air next to a drawn pixel.
        if (outline <= 0.0) discard;
        float n = Texel(tex, uv + vec2(outline, 0.0)).a + Texel(tex, uv - vec2(outline, 0.0)).a
                + Texel(tex, uv + vec2(0.0, outline)).a + Texel(tex, uv - vec2(0.0, outline)).a;
        if (n < 0.5) discard;
        return vec4(0.04, 0.04, 0.07, 1.0);
    }
    float fog = smoothstep(view_dist * 0.7, view_dist, distance(wpos.xz, focus.xz));
    vec3 rgb = mix(c.rgb, sky, fog);
    if (water > 0.5 && wpos.y < 1.5) {                              // (the water: the lowest ground)
        float s = sin(time * 1.7 + wpos.x * 0.21 + wpos.z * 0.13) * sin(time * 1.1 - wpos.x * 0.07 + wpos.z * 0.19);
        rgb *= 1.0 + 0.12 * s;
    }
    rgb = mix(rgb, vec3(dot(rgb, vec3(0.3, 0.59, 0.11))), mono);   // (monochrome: $2001 bit 0)
    return vec4(rgb * tint * daylight, 1.0);                        // (colour emphasis, time of day)
}
#endif
]]

-- Soft shadows under the sprites (blended, so a shader of their own).
Sh.SHADOW = [[
uniform mat4 proj;
uniform mat4 view;
uniform vec3 focus;
uniform float view_dist;
varying vec3 wpos;
#ifdef VERTEX
vec4 position(mat4 transform_projection, vec4 vertex_position) {
    wpos = vertex_position.xyz;
    return proj * view * vertex_position;
}
#endif
#ifdef PIXEL
vec4 effect(vec4 color, Image tex, vec2 uv, vec2 screen_coords) {
    float fog = smoothstep(view_dist * 0.7, view_dist, distance(wpos.xz, focus.xz));
    return vec4(0.0, 0.0, 0.0, Texel(tex, uv).a * 0.45 * (1.0 - fog));
}
#endif
]]

-- Tilt-shift: a separable blur that grows with the distance from the
-- screen's middle row (where the party is), HD-2D's miniature look.
Sh.BLUR = [[
uniform vec2 dir;          // one texel along the pass
uniform float strength;
vec4 effect(vec4 color, Image tex, vec2 uv, vec2 screen_coords) {
    float a = smoothstep(0.12, 0.48, abs(uv.y - 0.5)) * strength;
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int i = -4; i <= 4; i++) {
        float w = exp(-float(i * i) / 8.0);
        sum += Texel(tex, uv + dir * float(i) * a * 1.6) * w;
        wsum += w;
    }
    return sum / wsum * color;
}
]]

return Sh
