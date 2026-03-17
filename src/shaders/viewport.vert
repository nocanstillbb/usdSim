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
        vec4 clipCenter = ubo.mvp * vec4(meshCenter, 1.0);

        // Screen-space direction from center to this vertex
        vec2 screenPos    = clipPos.xy / clipPos.w;
        vec2 screenCenter = clipCenter.xy / clipCenter.w;
        vec2 dir = screenPos - screenCenter;
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
