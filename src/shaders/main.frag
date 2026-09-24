#version 330 core

uniform sampler2D image;

in mat3 T; // projection matrix

void main(void) {
    vec3 pos_homogeneous = T * vec3(gl_FragCoord.xy, 1.0);
    vec2 pos = pos_homogeneous.xy / pos_homogeneous.z;

    gl_FragColor = texture(image, pos);
}