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
    vec4 shadowExtra;    // x=lightRadius, y=nearP, z=farP, w=shadowMode(0=ortho,1=persp,2=cube)
    vec4 shadowLightPos; // xyz=light position (cube shadow), w=farPlane
};
layout(binding = 2) uniform sampler2D shadowMap;
layout(binding = 3) uniform samplerCube cubeShadowMap;

// --- PCSS helpers ---

// Interleaved gradient noise for per-pixel random rotation (Jorge Jimenez)
float interleavedGradientNoise(vec2 screenPos) {
    return fract(52.9829189 * fract(0.06711056 * screenPos.x + 0.00583715 * screenPos.y));
}

// Vogel disk sample: generates N evenly distributed points on a unit disk
vec2 vogelDiskSample(int sampleIdx, int numSamples, float rotation) {
    float goldenAngle = 2.399963;  // pi * (3 - sqrt(5))
    float r = sqrt(float(sampleIdx) + 0.5) / sqrt(float(numSamples));
    float theta = float(sampleIdx) * goldenAngle + rotation;
    return vec2(cos(theta), sin(theta)) * r;
}

#define PCSS_BLOCKER_SAMPLES 16
#define PCSS_PCF_SAMPLES 32

// Step 1: Blocker search — find average depth of occluders in a search region
float findBlockerDepth(vec2 uv, float receiverDepth, float searchRadius, float bias) {
    float blockerSum = 0.0;
    int blockerCount = 0;
    float rotation = interleavedGradientNoise(gl_FragCoord.xy) * 6.2831853;

    for (int i = 0; i < PCSS_BLOCKER_SAMPLES; ++i) {
        vec2 offset = vogelDiskSample(i, PCSS_BLOCKER_SAMPLES, rotation) * searchRadius;
        float sampleDepth = texture(shadowMap, uv + offset).r;
        if (receiverDepth - bias > sampleDepth) {
            blockerSum += sampleDepth;
            blockerCount++;
        }
    }
    return blockerCount > 0 ? blockerSum / float(blockerCount) : -1.0;
}

// Step 3: Variable-width PCF
float pcfFilter(vec2 uv, float receiverDepth, float filterRadius, float bias) {
    float shadow = 0.0;
    float rotation = interleavedGradientNoise(gl_FragCoord.xy) * 6.2831853;

    for (int i = 0; i < PCSS_PCF_SAMPLES; ++i) {
        vec2 offset = vogelDiskSample(i, PCSS_PCF_SAMPLES, rotation) * filterRadius;
        float sampleDepth = texture(shadowMap, uv + offset).r;
        shadow += (receiverDepth - bias > sampleDepth) ? 1.0 : 0.0;
    }
    return shadow / float(PCSS_PCF_SAMPLES);
}

// --- Cube shadow PCSS (omnidirectional sphere lights) ---

float cubeShadowPCSS(vec3 worldPos) {
    vec3 lightPos = shadowLightPos.xyz;
    float farPlane = shadowLightPos.w;
    float lightRadius = shadowExtra.x;

    vec3 N = normalize(vNormal);

    // Use ORIGINAL worldPos for sampling direction — normal offset can push
    // the direction past thin occluders (partitions, shelves), causing leaks.
    vec3 fragToLight = worldPos - lightPos;
    float currentDist = length(fragToLight);
    float currentDepth = currentDist / farPlane;
    vec3 dir = normalize(fragToLight);

    // Slope-scaled depth bias (compensates for geometry expansion in shadow_cube.vert
    // and cubemap texel discretization). Larger at grazing angles where a single
    // cubemap texel spans a wider depth range.
    float NdotL = abs(dot(N, dir));
    float bias = mix(0.006, 0.0015, NdotL) * currentDepth;

    // Build tangent frame around direction for disk sampling
    vec3 up = abs(dir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, dir));
    up = cross(dir, right);

    // Light angular size controls penumbra
    float lightSizeAngular = lightRadius / max(currentDist, 0.001);

    // Ensure minimum filter radius to avoid moiré from cubemap texel grid
    float texelAngle = 2.0 / 2048.0; // ~2 cubemap texels in angular space
    lightSizeAngular = max(lightSizeAngular, texelAngle);

    float rotation = interleavedGradientNoise(gl_FragCoord.xy) * 6.2831853;

    // Step 1: Blocker search — always check center direction first
    float searchRadius = min(lightSizeAngular * 0.5, 0.3); // cap to avoid overshooting
    float blockerSum = 0.0;
    int blockerCount = 0;

    // Center sample (most important — directly toward the light)
    float centerDepth = texture(cubeShadowMap, fragToLight).r;
    if (currentDepth - bias > centerDepth) {
        blockerSum += centerDepth;
        blockerCount++;
    }

    // Disk samples around center
    for (int i = 0; i < PCSS_BLOCKER_SAMPLES; ++i) {
        vec2 diskOff = vogelDiskSample(i, PCSS_BLOCKER_SAMPLES, rotation) * searchRadius;
        vec3 sampleDir = normalize(dir + right * diskOff.x + up * diskOff.y);
        float sampleDepth = texture(cubeShadowMap, sampleDir).r;
        if (currentDepth - bias > sampleDepth) {
            blockerSum += sampleDepth;
            blockerCount++;
        }
    }

    if (blockerCount == 0)
        return 0.0;

    float avgBlockerDepth = blockerSum / float(blockerCount);

    // Step 2: Penumbra estimation
    float penumbraWidth = (currentDepth - avgBlockerDepth) / max(avgBlockerDepth, 0.0001) * lightSizeAngular;
    penumbraWidth = clamp(penumbraWidth, 0.002, lightSizeAngular * 4.0);

    // Step 3: Variable-width PCF
    float shadow = 0.0;
    for (int i = 0; i < PCSS_PCF_SAMPLES; ++i) {
        vec2 diskOff = vogelDiskSample(i, PCSS_PCF_SAMPLES, rotation) * penumbraWidth;
        vec3 sampleDir = normalize(dir + right * diskOff.x + up * diskOff.y);
        float sampleDepth = texture(cubeShadowMap, sampleDir).r;
        shadow += (currentDepth - bias > sampleDepth) ? 1.0 : 0.0;
    }
    return shadow / float(PCSS_PCF_SAMPLES);
}

// --- 2D shadow PCSS (directional/rect/disk lights) ---

float pcssShadow(vec2 uv, float receiverDepth, float bias) {
    float lightRadius = shadowExtra.x;
    float nearP = shadowExtra.y;
    float farP = shadowExtra.z;

    // Light size in shadow-map UV space — normalize by frustum half-width
    // For perspective: frustum half-width at distance d ≈ d * nearP_ratio
    // Use a practical normalization: lightRadius relative to scene coverage
    float lightSizeUV = lightRadius * shadowParams.x * 8.0;  // scale by texel size for practical range

    // Clamp to reasonable range — if lightRadius is 0, fall back to basic PCF
    if (lightSizeUV < 0.0001) {
        // Basic 3x3 PCF fallback for non-sphere lights
        vec2 texelSize = shadowParams.xy;
        float shadow = 0.0;
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                float d = texture(shadowMap, uv + vec2(x, y) * texelSize).r;
                shadow += (receiverDepth - bias > d) ? 1.0 : 0.0;
            }
        }
        return shadow / 9.0;
    }

    // Step 1: Blocker search with radius proportional to light size
    float searchRadius = lightSizeUV;
    float avgBlockerDepth = findBlockerDepth(uv, receiverDepth, searchRadius, bias);

    if (avgBlockerDepth < 0.0)
        return 0.0;  // No blockers found — fully lit

    // Step 2: Penumbra estimation
    float penumbraWidth = (receiverDepth - avgBlockerDepth) / avgBlockerDepth * lightSizeUV;
    penumbraWidth = clamp(penumbraWidth, shadowParams.x, lightSizeUV * 4.0);

    // Step 3: Variable-width PCF
    return pcfFilter(uv, receiverDepth, penumbraWidth, bias);
}

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
        vec3 shadowDiffuse = vec3(0.0);  // contribution from lights affected by shadow
        vec3 otherDiffuse = vec3(0.0);   // contribution from lights NOT affected by shadow
        int shadowIdx = numLights.y;     // index of shadow-casting light (-1 = none)

        int count = numLights.x;
        for (int i = 0; i < count; ++i) {
            int ltype = int(lights[i].posType.w + 0.5);
            vec3 lcol = lights[i].color.rgb;
            vec3 contrib = vec3(0.0);

            if (ltype == 0) {
                // Distant light
                vec3 L = normalize(lights[i].dirRadius.xyz);
                float d = max(dot(N, L), 0.0);
                contrib = lcol * d;
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
                contrib = lcol * NdotL * atten;
            } else {
                // Dome light — hemisphere ambient (more light from above)
                float hemi = 0.5 + 0.5 * N.y;
                ambient += lcol * mix(0.3, 1.0, hemi);
            }

            // Shadow only affects the shadow-casting light's contribution.
            // Other lights are unshadowed — light always overrides shadow.
            if (i == shadowIdx)
                shadowDiffuse += contrib;
            else
                otherDiffuse += contrib;
        }

        // Shadow mapping — applied only to the shadow-casting light
        float shadow = 0.0;
        if (shadowParams.w > 0.5) {
            if (shadowExtra.w > 1.5) {
                // Cube shadow mode (sphere lights — omnidirectional)
                shadow = cubeShadowPCSS(vWorldPos);
            } else {
                // 2D shadow mode (directional/rect/disk lights — PCSS)
                vec4 lsPos = lightVP * vec4(vWorldPos, 1.0);
                vec3 proj = lsPos.xyz / lsPos.w;
                proj = proj * 0.5 + 0.5;
                if (proj.z > 0.0 && proj.z <= 1.0 &&
                    proj.x >= 0.0 && proj.x <= 1.0 &&
                    proj.y >= 0.0 && proj.y <= 1.0)
                {
                    float maxBias = shadowParams.z;
                    float minBias = maxBias * 0.4; // higher floor prevents shadow acne on perpendicular faces
                    vec3 lightFwd = normalize(vec3(lightVP[0][2], lightVP[1][2], lightVP[2][2]));
                    float cosTheta = abs(dot(N, lightFwd));
                    float bias = mix(maxBias, minBias, cosTheta);
                    shadow = pcssShadow(proj.xy, proj.z, bias);
                }
            }
        }
        vec3 diffuse = shadowDiffuse * (1.0 - shadow) + otherDiffuse;

        vec3 lit = vColor * (ambient + diffuse);
        // Reinhard tonemapping: prevents blowout while preserving color
        fragColor = vec4(lit / (lit + vec3(1.0)), 1.0);

        // DEBUG: uncomment to visualize cubemap shadow depth
        // if (shadowExtra.w > 1.5) {
        //     vec3 d = vWorldPos - shadowLightPos.xyz;
        //     float cd = length(d) / shadowLightPos.w;
        //     float sd = texture(cubeShadowMap, d).r;
        //     float sh = (cd - 0.001*cd > sd && sd > 0.00001) ? 1.0 : 0.0;
        //     fragColor = vec4(sd*10.0, cd*10.0, sh, 1.0);
        // }
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
