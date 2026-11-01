// Nagare glass: a lens over the app's own pixels. The glass pass outputs premultiplied colour, so its shadow darkens whatever is already below it.

cbuffer Params : register(b0)
{
    float2 viewport;
    float2 texelStep; // blur direction, in uv
    float4 quad;      // pixels covered by the draw, shadow included
    float4 rect;      // the glass itself: x0, y0, x1, y1
    float radius;
    float band;       // width of the curved edge in pixels
    float refraction; // how far the edge bends the image, in pixels
    float dispersion; // 0..1 share of the bend that splits red from blue
    float4 tint;      // rgb + mix
    float rimLight;
    float rimDark;
    float shadow;
    float clarity;    // 0 frosted, 1 clear
    float saturation;
    float3 padding;
};

Texture2D scene : register(t0);
Texture2D blurred : register(t1);
SamplerState linearClamp : register(s0);

struct VsOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VsOut vsQuad(uint id : SV_VertexID)
{
    float2 corner = float2(id & 1, (id >> 1) & 1);
    float2 px = lerp(quad.xy, quad.zw, corner);
    VsOut output;
    output.position = float4(px.x / viewport.x * 2.0 - 1.0, 1.0 - px.y / viewport.y * 2.0, 0.0, 1.0);
    output.uv = px / viewport;
    return output;
}

// Four bilinear taps cover every source pixel, so fine dots don't shimmer in the small copy.
float4 psDown(VsOut input) : SV_Target
{
    float3 colour = scene.SampleLevel(linearClamp, input.uv + texelStep * float2(-1.0, -1.0), 0).rgb;
    colour += scene.SampleLevel(linearClamp, input.uv + texelStep * float2(1.0, -1.0), 0).rgb;
    colour += scene.SampleLevel(linearClamp, input.uv + texelStep * float2(-1.0, 1.0), 0).rgb;
    colour += scene.SampleLevel(linearClamp, input.uv + texelStep * float2(1.0, 1.0), 0).rgb;
    return float4(colour * 0.25, 1.0);
}

// Nine taps folded into five by bilinear filtering.
float4 psBlur(VsOut input) : SV_Target
{
    static const float offsets[3] = { 0.0, 1.3846153846, 3.2307692308 };
    static const float weights[3] = { 0.2270270270, 0.3162162162, 0.0702702703 };
    float3 colour = scene.SampleLevel(linearClamp, input.uv, 0).rgb * weights[0];
    for (int i = 1; i < 3; ++i)
    {
        colour += scene.SampleLevel(linearClamp, input.uv + texelStep * offsets[i], 0).rgb * weights[i];
        colour += scene.SampleLevel(linearClamp, input.uv - texelStep * offsets[i], 0).rgb * weights[i];
    }
    return float4(colour, 1.0);
}

// A superellipse corner, the continuous curve Apple draws, rather than a circular arc.
float roundedDistance(float2 p)
{
    float2 centre = (rect.xy + rect.zw) * 0.5;
    float2 halfSize = (rect.zw - rect.xy) * 0.5;
    float2 q = abs(p - centre) - halfSize + radius;
    float2 outside = max(q, 0.0);
    float curve = pow(pow(outside.x, 2.6) + pow(outside.y, 2.6), 1.0 / 2.6);
    return curve + min(max(q.x, q.y), 0.0) - radius;
}

float3 sampleLens(float2 px)
{
    float2 uv = px / viewport;
    float3 frosted = blurred.SampleLevel(linearClamp, uv, 0).rgb;
    if (clarity <= 0.0)
    {
        return frosted;
    }
    return lerp(frosted, scene.SampleLevel(linearClamp, uv, 0).rgb, clarity);
}

float4 psGlass(VsOut input) : SV_Target
{
    float2 px = input.position.xy;
    float d = roundedDistance(px);

    if (d > 0.5)
    {
        // Outside the glass there is only its contact shadow, so skip the expensive path.
        return float4(0.0, 0.0, 0.0, exp(-d / 11.0) * shadow);
    }

    float2 e = float2(0.75, 0.0);
    float2 normal = float2(roundedDistance(px + e.xy) - roundedDistance(px - e.xy), roundedDistance(px + e.yx) - roundedDistance(px - e.yx));
    float gradient = length(normal);
    normal = gradient > 0.0001 ? normal / gradient : float2(0.0, 0.0);

    // The surface is flat in the middle and falls away over the band, like a drop of water.
    float depth = saturate(1.0 + d / band);
    float bend = depth * depth * depth * refraction;
    float2 offset = -normal * bend;

    float3 colour;
    if (dispersion > 0.0 && depth > 0.02)
    {
        float split = dispersion;
        colour.r = sampleLens(px + offset * (1.0 + split)).r;
        colour.g = sampleLens(px + offset).g;
        colour.b = sampleLens(px + offset * (1.0 - split)).b;
    }
    else
    {
        colour = sampleLens(px + offset);
    }

    float luma = dot(colour, float3(0.2126, 0.7152, 0.0722));
    colour = lerp(luma.xxx, colour, saturation);
    colour = lerp(colour, tint.rgb, tint.a);

    // Light from the upper left catches the rim, and its opposite side catches the bounce.
    float2 light = normalize(float2(-0.45, -0.89));
    float facing = dot(normal, light);
    float rim = smoothstep(-1.8, -0.4, d);
    float catchLight = pow(abs(facing), 1.6) * (facing > 0.0 ? 1.0 : 0.55);
    colour += rimLight * rim * catchLight;
    colour *= 1.0 - rimDark * smoothstep(-5.0, -0.5, d) * (1.0 - rim);

    // A soft glow inside the edge gives the curve its thickness.
    colour += depth * depth * 0.035;

    // The antialiased edge fades into the shadow around it.
    float coverage = saturate(0.5 - d);
    return float4(colour * coverage, coverage + (1.0 - coverage) * shadow);
}
