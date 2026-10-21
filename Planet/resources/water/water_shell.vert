#version 330 core

void main() {
    // One oversized triangle covers the viewport; no vertex/index buffers.
    // Do not project the mean sea sphere here: crests can cross its silhouette.
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
