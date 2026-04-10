#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal; // needed for vertex layout compatibility

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

    // Expand geometry AWAY FROM LIGHT by ~2 cubemap texels.
    // This GROWS the shadow outward (covers junctions where shelf meets panel).
    // Normal-direction expansion merely SHIFTS the shadow, leaving a gap at
    // the far edge — exactly where occluder meets receiver.
    // Only gl_Position is expanded — vWorldPos stays original, so stored depth is exact.
    float distToLight = length(vWorldPos - ubo.lightDir.xyz);
    float texelSize = distToLight / 2048.0;
    float localScale = length(ubo.modelMat[0].xyz);
    vec3 worldLightDir = normalize(vWorldPos - ubo.lightDir.xyz); // away from light
    vec3 localLightDir = normalize(transpose(mat3(ubo.modelMat)) * worldLightDir);
    vec3 expandedPos = inPos + localLightDir * (texelSize * 2.0 / max(localScale, 0.0001));
    gl_Position = ubo.mvp * vec4(expandedPos, 1.0);
}
