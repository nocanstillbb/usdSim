#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(std140, binding = 0) uniform PerObject {
    mat4 mvp;
    mat4 modelMat;
    vec4 color;      // rgb=color, a unused
    vec4 lightDir;   // xyz=direction, w unused
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;

void main()
{
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    vNormal = normalize(mat3(ubo.modelMat) * inNormal);
    vColor  = ubo.color.rgb;
}
