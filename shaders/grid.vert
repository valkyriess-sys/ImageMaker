#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} pc;

layout(set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec4 vColor;

void main() {
    gl_Position = ubo.proj * ubo.view * pc.model * vec4(inPosition, 1.0);
    vColor = pc.color;
}
