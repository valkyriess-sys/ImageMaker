#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} pc;

layout(set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragPos;
layout(location = 2) out vec4 fragColor;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragNormal = mat3(pc.model) * inNormal;
    fragPos = worldPos.xyz;
    fragColor = pc.color;
}
