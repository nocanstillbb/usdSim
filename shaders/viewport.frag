#version 440

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform PerObject {
    mat4 mvp;
    mat4 modelMat;
    vec4 color;
    vec4 lightDir;
} ubo;

void main()
{
    vec3 N    = normalize(vNormal);
    vec3 L    = normalize(ubo.lightDir.xyz);
    float d   = abs(dot(N, L));
    float lit = 0.2 + 0.8 * d;
    fragColor = vec4(vColor * lit, 1.0);
}
