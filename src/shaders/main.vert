#version 330 core

void main(void) {
    gl_Position = vec4(gl_VertexID == 0 ? -1.0 : 1.0, gl_VertexID == 0 ? -1.0 : 1.0, 0.0, 1.0);
}