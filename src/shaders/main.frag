#version 330 core

uniform sampler2D image;
uniform vec2 window_size;
uniform vec2 image_size;
uniform vec2 offset;
uniform float scale;

in vec2 vUV;
out vec4 FragColor;

void main(void) {
    vec2 uv = vUV * scale + offset;
    FragColor = texture(image, uv * window_size/image_size);
}
