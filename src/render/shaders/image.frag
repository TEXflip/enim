#version 330 core

uniform sampler2D image;
uniform vec2 scale;
uniform vec2 offset;
uniform bool flip_y; // texture rows are stored bottom-up

in vec2 vUV;
out vec4 FragColor;

void main(void) {
    vec2 uv = vUV * scale + offset; // top-down image coordinates in [0, 1]
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        discard;
    if (flip_y)
        uv.y = 1.0 - uv.y;
    FragColor = texture(image, uv);
}
