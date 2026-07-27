#version 450
// Inverted-hull outline: expand vertices along normals, render backfaces

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} pc;

layout(set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
} ubo;

void main() {
    // Expand along world-space normal
    float outlineThickness = 0.04;
    vec3 worldNormal = normalize(mat3(pc.model) * inNormal);
    vec4 worldPos = pc.model * vec4(inPosition + inNormal * outlineThickness, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
}
