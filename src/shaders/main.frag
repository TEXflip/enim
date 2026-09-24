#version 330 core
#extension GL_ARB_separate_shader_objects : enable

uniform sampler2D image;

// in mat3 T; // projection matrix
layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 FragColor;

void main(void) {
    // vec3 pos_homogeneous = T * vec3(gl_FragCoord.xy, 1.0);
    // vec2 pos = pos_homogeneous.xy / pos_homogeneous.z;

    vec2 normalized_pos = (frag_position + 1.0) / 2.0;
    FragColor = vec4(normalized_pos.xy, 0.0, 1.0);
}