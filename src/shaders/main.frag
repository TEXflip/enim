#version 330 core

uniform sampler2D image;
uniform vec2 scale;
uniform vec2 offset;

in vec2 vUV;
out vec4 FragColor;

void main(void) {
    vec2 uv = vUV * scale + offset;
    uv.y = 1.0 - uv.y;
    FragColor = texture(image, uv);
}
