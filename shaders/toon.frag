#version 450
// Toon/Cel shading with rim light (Guilty Gear Strive style)

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec4 fragColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 2.0, 1.5));
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.camPos.xyz - fragPos);
    vec3 H = normalize(lightDir + V);

    // Cel banding: discrete steps (3 tones)
    float diff = max(dot(N, lightDir), 0.0);
    float bands = 3.0;
    float celDiff = floor(diff * bands) / bands;

    // Ambient base
    float ambient = 0.2;

    // Rim light — thick, Guilty Gear style
    float rim = 1.0 - abs(dot(N, V));
    rim = pow(rim, 3.0) * 0.45;

    // Sharp anime specular (threshold-based)
    float spec = pow(max(dot(H, N), 0.0), 128.0);
    float specThreshold = step(0.7, spec) * 0.55;

    float lighting = ambient + celDiff * 0.65 + rim + specThreshold;

    outColor = vec4(fragColor.rgb * lighting, fragColor.a);
}
