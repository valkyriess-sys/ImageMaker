#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 2.0, 1.5));
    vec3 N = normalize(fragNormal);
    float diff = max(dot(N, lightDir), 0.0);
    float ambient = 0.25;
    float lighting = ambient + diff * 0.75;
    outColor = vec4(fragColor.rgb * lighting, fragColor.a);
}
