#include "common/fullscreen_vs_inout.hlsli"
#include "post/tonemappers.hlsli"

Texture2D g_texture                             : register(t0, space0);
SamplerState g_sampler                          : register(s0, space0);

//
// dithering

uint wang_hash(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

float wang_float(uint hash)
{
    return hash / float(0x7FFFFFFF) / 2.0;
}

float3 dither(in float3 color, float2 screen_pixel, int divisor) {
    uint seed = uint(screen_pixel.x) + uint(screen_pixel.y) * 8096;
    float r = wang_float(wang_hash(seed * 3 + 0));
    float g = wang_float(wang_hash(seed * 3 + 1));
    float b = wang_float(wang_hash(seed * 3 + 2));
    float3 random = float3(r, g, b);

    return color + ((random - .5) / divisor);
}


float4 main_ps(vs_out In) : SV_TARGET
{
    float3 hdr = g_texture.Sample(g_sampler, In.Texcoord).rgb;

    //if (In.Texcoord.x < 0.5f)
    //    return float4(hdr, 1.f);

    // tonemap
    float3 color = tonemap_aces_fitted(hdr);

    // gamma
    color = pow(color, 1.0f / 2.224f);

    // dither
    color = dither(color, In.SV_P.xy, 100);

    return float4(color, 1.f);
}
