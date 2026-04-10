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
    // Store original world position for accurate depth
    vWorldPos = (ubo.modelMat * vec4(inPos, 1.0)).xyz;

    // Expand clip position along normals by ~2 cubemap texels.
    // This makes surface edges cover adjacent texels, preventing
    // light leaks at junctions (shelf meets wall) and self-shadow artifacts
    // (since viewport.frag uses no normal offset for direction accuracy).
    // Only gl_Position is expanded — vWorldPos stays original, so stored depth is exact.
    float distToLight = length(vWorldPos - ubo.lightDir.xyz);
    float texelSize = distToLight / 2048.0;
    float localScale = length(ubo.modelMat[0].xyz);
    vec3 expandedPos = inPos + normalize(inNormal) * (texelSize * 2.0 / max(localScale, 0.0001));
    gl_Position = ubo.mvp * vec4(expandedPos, 1.0);
}
