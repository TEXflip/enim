#version 330 core

uniform sampler2D image;
uniform vec2 scale;
uniform vec2 offset;
uniform bool flip_y; // texture rows are stored bottom-up
uniform vec2 region_min; // image region the texture covers, in [0, 1] image coordinates (may reach past 1)
uniform vec2 region_max;
// image coordinates -> texel coordinates of the texture, clamped (a tile ring wraps with GL_REPEAT: the clamp keeps
// filtering from reaching the slots past the ring's window, like GL_CLAMP_TO_EDGE)
uniform vec2 texel_scale;
uniform vec2 texel_offset;
uniform vec2 texel_min;
uniform vec2 texel_max;
uniform vec2 texture_size;

in vec2 vUV;
out vec4 FragColor;

void main(void) {
    vec2 uv = vUV * scale + offset; // top-down image coordinates in [0, 1]
    if (any(lessThan(uv, max(region_min, vec2(0.0)))) || any(greaterThan(uv, min(region_max, vec2(1.0)))))
        discard;
    vec2 texel = clamp(uv * texel_scale + texel_offset, texel_min, texel_max);
    vec2 tex_uv = texel / texture_size;
    if (flip_y)
        tex_uv.y = 1.0 - tex_uv.y;
    FragColor = texture(image, tex_uv);
}
