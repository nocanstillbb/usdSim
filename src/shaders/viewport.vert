#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(std140, binding = 0) uniform PerObject {
    mat4 mvp;
    mat4 modelMat;
    vec4 color;      // rgb=color, a unused
    vec4 lightDir;   // lit: xyz=cameraEye, w=0; outline: xyz=meshCenter, w=push
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec3 vWorldPos;

void main()
{
    float push = ubo.lightDir.w;
    vec4 clipPos = ubo.mvp * vec4(inPos, 1.0);

    if (push > 0.0) {
        // Outline mode: expand in screen space from projected mesh center
        vec3 meshCenter = ubo.lightDir.xyz;
        vec3 modelDir = inPos - meshCenter;

        float sx = length(ubo.modelMat[0].xyz);
        float sy = length(ubo.modelMat[1].xyz);
        float sz = length(ubo.modelMat[2].xyz);
        vec3 compensated = modelDir / vec3(sx, sy, sz);

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
    vWorldPos = (ubo.modelMat * vec4(inPos, 1.0)).xyz;
}
