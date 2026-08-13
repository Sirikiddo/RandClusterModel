#version 330 core

layout(location=0) in vec3 aPos;

uniform mat4 uMVP;
uniform float uWaterShellOuterRadius;

void main() {
    vec3 proxyDir = normalize(aPos);
    gl_Position = uMVP * vec4(proxyDir * uWaterShellOuterRadius, 1.0);
}
