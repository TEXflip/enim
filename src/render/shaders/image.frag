#version 330 core

uniform sampler2D image;
uniform vec2 scale;
uniform vec2 offset;
uniform bool flip_y; // texture rows are stored bottom-up
uniform vec2 region_min; // image region the texture covers, in [0, 1] image coordinates (may reach past 1)
uniform vec2 region_max;

in vec2 vUV;
out vec4 FragColor;

void main(void) {
    vec2 uv = vUV * scale + offset; // top-down image coordinates in [0, 1]
    if (any(lessThan(uv, max(region_min, vec2(0.0)))) || any(greaterThan(uv, min(region_max, vec2(1.0)))))
        discard;
    vec2 tex_uv = (uv - region_min) / (region_max - region_min);
    if (flip_y)
        tex_uv.y = 1.0 - tex_uv.y;
    FragColor = texture(image, tex_uv);
}
