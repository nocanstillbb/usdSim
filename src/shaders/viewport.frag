#version 440

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform PerObject {
    mat4 mvp;
    mat4 modelMat;
    vec4 color;
    vec4 lightDir;   // lit: xyz=cameraEye, w=0; outline: xyz=meshCenter, w=push
} ubo;

#define MAX_LIGHTS 16
struct Light {
    vec4 posType;    // xyz=position, w=type (0=distant, 1=point, 2=dome)
    vec4 dirRadius;  // xyz=direction, w=radius
    vec4 color;      // rgb=effective color, a=unused
};
layout(std140, binding = 1) uniform SceneLights {
    Light lights[MAX_LIGHTS];
    ivec4 numLights; // x=count
};

void main()
{
    float useLit = step(0.5, ubo.color.a);
    if (useLit > 0.5) {
        // Lit shading with scene lights
        vec3 N = normalize(vNormal);

        // Flip normal to face camera (handles inconsistent USD mesh winding)
        vec3 V = normalize(ubo.lightDir.xyz - vWorldPos);
        if (dot(N, V) < 0.0) N = -N;

        vec3 ambient = vec3(0.05);
        vec3 diffuse = vec3(0.0);

        int count = numLights.x;
        for (int i = 0; i < count; ++i) {
            int ltype = int(lights[i].posType.w + 0.5);
            vec3 lcol = lights[i].color.rgb;

            if (ltype == 0) {
                // Distant light
                vec3 L = normalize(lights[i].dirRadius.xyz);
                float d = max(dot(N, L), 0.0);
                diffuse += lcol * d;
            } else if (ltype == 1) {
                // Point light — Hydra Storm style: 1/(π*d²) attenuation
                vec3 toLight = lights[i].posType.xyz - vWorldPos;
                float dist2 = dot(toLight, toLight);
                float dist = sqrt(dist2);
                vec3 L = toLight / max(dist, 0.0001);
                float NdotL = max(dot(N, L), 0.0);
                // Small epsilon to avoid singularity at dist=0
                float atten = 1.0 / (3.14159 * max(dist2, 0.0001));
                diffuse += lcol * NdotL * atten;
            } else {
                // Dome light (ambient)
                ambient += lcol;
            }
        }

        vec3 lit = vColor * (ambient + diffuse);
        // Reinhard tonemapping: prevents blowout while preserving color
        fragColor = vec4(lit / (lit + vec3(1.0)), 1.0);
    } else {
        // Flat color (outline / wireframe)
        fragColor = vec4(vColor, 1.0);
    }
}
