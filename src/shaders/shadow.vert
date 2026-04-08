#version 440

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal; // unused, needed for vertex layout compatibility

layout(std140, binding = 0) uniform PerObject {
    mat4 mvp;
    mat4 modelMat;
    vec4 color;
    vec4 lightDir;
} ubo;

void main()
{
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
}
