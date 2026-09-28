#version 330 core

uniform sampler2D image;

// in mat3 T; // projection matrix
in vec2 vUV;
out vec4 FragColor;

void main(void) {
    // vec3 pos_homogeneous = T * vec3(gl_FragCoord.xy, 1.0);
    // vec2 pos = pos_homogeneous.xy / pos_homogeneous.z;

    vec2 normalized_pos = (vUV + 1.0) / 2.0;
    // DEBUG: Check if we're out of bounds
    if (normalized_pos.x < 0.0 || normalized_pos.x > 1.0 || 
        normalized_pos.y < 0.0 || normalized_pos.y > 1.0) {
        FragColor = vec4(1.0, 0.0, 0.0, 1.0); // Red = out of bounds
    } else {
        FragColor = texture(image, normalized_pos);
    }
}