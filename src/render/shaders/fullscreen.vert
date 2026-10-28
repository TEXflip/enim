#version 330 core

// Fullscreen triangle from gl_VertexID (draw 3 vertices, no attributes); the viewport selects the target rectangle.
void main(void) {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
