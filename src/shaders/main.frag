#version 330 core

uniform sampler2D image;
uniform vec2 window_size;
uniform vec2 image_size;
uniform vec2 offset;
uniform float zoom;

in vec2 frag_position;
out vec4 FragColor;

void main(void) {
    vec2 uv = frag_position * exp(zoom) + offset;
    FragColor = texture(image, uv * window_size/image_size + 0.5);
}
