#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal; // unused, needed for vertex layout compatibility

layout(location = 0) out vec3 vWorldPos;

layout(std140, binding = 0) uniform PerObject {
    mat4 mvp;
    mat4 modelMat;
    vec4 color;      // unused
    vec4 lightDir;   // xyz=lightPos, w=farPlane
} ubo;

void main()
{
    // Original world position — used for depth calculation (no expansion)
    vWorldPos = (ubo.modelMat * vec4(inPos, 1.0)).xyz;

    // Expand clip position along normals by ~2 cubemap texels.
    // This makes surface edges cover adjacent texels (prevents junction leak),
    // but depth is stored from the ORIGINAL position (prevents self-shadow).
    vec3 worldNormal = normalize(mat3(ubo.modelMat) * inNormal);
    float distToLight = length(vWorldPos - ubo.lightDir.xyz);
    float texelSize = distToLight / 1024.0; // ~2 texels at this distance
    float localScale = length(ubo.modelMat[0].xyz);
    vec3 expandedPos = inPos + normalize(inNormal) * (texelSize / max(localScale, 0.0001));
    gl_Position = ubo.mvp * vec4(expandedPos, 1.0);
}
