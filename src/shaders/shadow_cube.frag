#version 440

layout(location = 0) in vec3 vWorldPos;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform PerObject {
    mat4 mvp;
    mat4 modelMat;
    vec4 color;
    vec4 lightDir;   // xyz=lightPos, w=farPlane
} ubo;

void main()
{
    float dist = length(vWorldPos - ubo.lightDir.xyz);
    // Store exact linear distance. Self-shadow prevention is handled by
    // normal offset in viewport.frag. Junction coverage is handled by
    // geometry expansion in this vertex shader.
    fragColor = vec4(dist / ubo.lightDir.w, 0.0, 0.0, 0.0);
}
