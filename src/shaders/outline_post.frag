#version 440

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D maskTex;

layout(std140, binding = 1) uniform OutlineParams {
    vec4 outlineColor;    // rgb = outline color
    vec4 params;          // xy = texelSize, z = radius, w = flipY
};

void main()
{
    vec2 uv = vUV;
    if (params.w > 0.5) uv.y = 1.0 - uv.y;

    float center = texture(maskTex, uv).a;
    if (center > 0.5) discard;   // inside mask — no outline here

    vec2 texel = params.xy;
    float rSq  = params.z * params.z;

    // Search neighbours within circular radius (max 3)
    float hit = 0.0;
    for (int y = -3; y <= 3; ++y)
        for (int x = -3; x <= 3; ++x)
            if (float(x*x + y*y) <= rSq)
                hit = max(hit, texture(maskTex, uv + vec2(float(x), float(y)) * texel).a);

    if (hit < 0.5) discard;       // no mask neighbour — not an edge
    fragColor = vec4(outlineColor.rgb, 1.0);
}
