#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(std140, binding = 0) uniform PerObject {
    mat4 mvp;
    mat4 modelMat;
    vec4 color;      // rgb=color, a unused
    vec4 lightDir;   // xyz=direction (or meshCenter for outline), w=push amount
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;

void main()
{
    float push = ubo.lightDir.w;
    vec4 clipPos = ubo.mvp * vec4(inPos, 1.0);

    if (push > 0.0) {
        // Outline mode: expand in screen space from projected mesh center
        vec3 meshCenter = ubo.lightDir.xyz;
        vec3 modelDir = inPos - meshCenter;

        // Compensate non-uniform scale so push direction isn't distorted
        // by elongated transforms (e.g. thin poles with scale 0.03 x 8 x 0.03).
        // Dividing by column lengths removes the scale, then MVP * (dir/scale, 0)
        // effectively transforms through ViewProj * Rotation (no scale).
        float sx = length(ubo.modelMat[0].xyz);
        float sy = length(ubo.modelMat[1].xyz);
        float sz = length(ubo.modelMat[2].xyz);
        vec3 compensated = modelDir / vec3(sx, sy, sz);

        // Project compensated direction to clip space (w=0 → direction only)
        vec4 clipDir = ubo.mvp * vec4(compensated, 0.0);
        vec2 dir = clipDir.xy;
        float len = length(dir);
        if (len > 0.00001) {
            dir = normalize(dir);
            clipPos.xy += dir * push * clipPos.w;
        }
    }

    gl_Position = clipPos;
    vNormal = normalize(mat3(ubo.modelMat) * inNormal);
    vColor  = ubo.color.rgb;
}
