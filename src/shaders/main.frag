#version 330 core

uniform sampler2D image;
uniform vec2 scale;
uniform vec2 offset;

in vec2 vUV;
out vec4 FragColor;

void main(void) {
    FragColor = texture(image, vUV * scale + offset);
}
