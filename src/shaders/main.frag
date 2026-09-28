#version 330 core

uniform sampler2D image;

// in mat3 T; // projection matrix
in vec2 vUV;
out vec4 FragColor;

void main(void) {
    // vec3 pos_homogeneous = T * vec3(gl_FragCoord.xy, 1.0);
    // vec2 pos = pos_homogeneous.xy / pos_homogeneous.z;

    vec2 normalized_pos = (vUV + 1.0) / 2.0;
    FragColor = texture(image, normalized_pos);
}