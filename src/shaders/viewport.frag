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
    vec4 color;      // rgb=effective color, a=subtype (0=sphere,1=rect,2=disk,3=cyl)
    vec4 shapeSize;  // x=width, y=height, z=unused, w=unused
};
layout(std140, binding = 1) uniform SceneLights {
    Light lights[MAX_LIGHTS];
    ivec4 numLights; // x=count
    mat4 lightVP;        // light view-projection for shadow mapping
    vec4 shadowParams;   // x=texelW, y=texelH, z=bias, w=enabled
};
layout(binding = 2) uniform sampler2D shadowMap;

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
                // Point/area light — Hydra Storm style: 1/(π*d²) attenuation
                vec3 toLight = lights[i].posType.xyz - vWorldPos;
                float dist2 = dot(toLight, toLight);
                float dist = sqrt(dist2);
                vec3 L = toLight / max(dist, 0.0001);
                float NdotL = max(dot(N, L), 0.0);
                float atten = 1.0 / (3.14159 * max(dist2, 0.0001));

                vec3 emitDir = lights[i].dirRadius.xyz;
                float dirLen2 = dot(emitDir, emitDir);
                int subtype = int(lights[i].color.a + 0.5);

                if (dirLen2 > 0.001) {
                    vec3 eDir = normalize(emitDir);
                    float hemi = dot(eDir, -L);
                    if (hemi <= 0.0) { atten = 0.0; }
                    else if (subtype == 1 || subtype == 2) {
                        // Rect/Disk: project fragment onto light plane,
                        // compute ratio to light size → projector-style cutoff
                        vec3 right = normalize(cross(
                            abs(eDir.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0), eDir));
                        vec3 up2 = cross(eDir, right);
                        vec3 offset = vWorldPos - lights[i].posType.xyz;
                        float projDist = dot(offset, eDir);
                        if (projDist <= 0.0) { atten = 0.0; }
                        else {
                            float projX = dot(offset, right);
                            float projY = dot(offset, up2);
                            // Light size defines cone angle: lit area grows with distance
                            // At d=0: area = W×H. At d=hw: area = 2W×2H. etc.
                            float hw = lights[i].shapeSize.x * 0.5;
                            float hh = lights[i].shapeSize.y * 0.5;
                            if (subtype == 1) {
                                float effHW = hw + projDist * hw;
                                float effHH = hh + projDist * hh;
                                float fx = abs(projX) / effHW;
                                float fy = abs(projY) / effHH;
                                float rectFade = (1.0 - smoothstep(0.9, 1.0, fx))
                                               * (1.0 - smoothstep(0.9, 1.0, fy));
                                atten *= hemi * rectFade;
                            } else {
                                float r = lights[i].shapeSize.x;
                                float effR = r + projDist * r;
                                float fr = length(vec2(projX, projY)) / effR;
                                float diskFade = 1.0 - smoothstep(0.9, 1.0, fr);
                                atten *= hemi * diskFade;
                            }
                        }
                    } else {
                        atten *= max(hemi, 0.0); // sphere/cylinder: hemisphere
                    }
                }
                diffuse += lcol * NdotL * atten;
            } else {
                // Dome light — hemisphere ambient (more light from above)
                float hemi = 0.5 + 0.5 * N.y;
                ambient += lcol * mix(0.3, 1.0, hemi);
            }
        }

        // Shadow mapping (PCF 3x3) with slope-scaled bias
        float shadow = 0.0;
        if (shadowParams.w > 0.5) {
            vec4 lsPos = lightVP * vec4(vWorldPos, 1.0);
            vec3 proj = lsPos.xyz / lsPos.w;
            proj = proj * 0.5 + 0.5;
            if (proj.z > 0.0 && proj.z <= 1.0 &&
                proj.x >= 0.0 && proj.x <= 1.0 &&
                proj.y >= 0.0 && proj.y <= 1.0)
            {
                // Slope-scaled bias: large for grazing angles, tiny for perpendicular
                float maxBias = shadowParams.z;
                float minBias = maxBias * 0.1;
                // Approximate light direction from light-space Z axis
                vec3 lightFwd = normalize(vec3(lightVP[0][2], lightVP[1][2], lightVP[2][2]));
                float cosTheta = abs(dot(N, lightFwd));
                float bias = mix(maxBias, minBias, cosTheta);

                vec2 texelSize = shadowParams.xy;
                for (int x = -1; x <= 1; ++x) {
                    for (int y = -1; y <= 1; ++y) {
                        float d = texture(shadowMap, proj.xy + vec2(x, y) * texelSize).r;
                        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
                    }
                }
                shadow /= 9.0;
            }
        }
        diffuse *= (1.0 - shadow);

        vec3 lit = vColor * (ambient + diffuse);
        // Reinhard tonemapping: prevents blowout while preserving color
        fragColor = vec4(lit / (lit + vec3(1.0)), 1.0);
    } else {
        // Flat color (outline / wireframe)
        fragColor = vec4(vColor, 1.0);
    }

    // Grid mode: push depth slightly further to avoid z-fighting with co-planar geometry
    // NOTE: once gl_FragDepth is written on any path, ALL paths must write it.
    if (ubo.lightDir.w < -0.5) {
        gl_FragDepth = gl_FragCoord.z + 0.00005;
    } else {
        gl_FragDepth = gl_FragCoord.z;
    }
}
