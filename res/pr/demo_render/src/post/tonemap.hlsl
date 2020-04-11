#include "common/fullscreen_vs_inout.hlsl"

#define TONEMAP_GAMMA 2.224

Texture2D g_texture                             : register(t0, space0);
SamplerState g_sampler                          : register(s0, space0);

// Uncharted 2 Tonemapper
float3 tonemap_uncharted2(in float3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;

    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float3 tonemap_uc2(in float3 color)
{
    float W = 11.2;

    color *= 16;  // Hardcoded Exposure Adjustment

    float exposure_bias = 2.0f;
    float3 curr = tonemap_uncharted2(exposure_bias*color);

    float3 white_scale = 1.0f/tonemap_uncharted2(W);
    float3 ccolor = curr*white_scale;

    float3 ret = pow(abs(ccolor), TONEMAP_GAMMA); // gamma

    return ret;
}

// Filmic tonemapper
float3 tonemap_filmic(in float3 color)
{
    color = max(0, color - 0.004f);
    color = (color * (6.2f * color + 0.5f)) / (color * (6.2f * color + 1.7f)+ 0.06f);

    // result has 1/2.2 baked in
    return pow(color, TONEMAP_GAMMA);
}

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

float4 dither(in float4 color, float2 screen_pixel, int divisor) {
    uint seed = uint(screen_pixel.x) + uint(screen_pixel.y) * 8096;
    float r = wang_float(wang_hash(seed * 3 + 0));
    float g = wang_float(wang_hash(seed * 3 + 1));
    float b = wang_float(wang_hash(seed * 3 + 2));
    float3 random = float3(r, g, b);

    return color + float4((random - .5) / divisor, 1.f);
}


float4 main_ps(vs_out In) : SV_TARGET
{
    float4 hdr = g_texture.Sample(g_sampler, In.Texcoord);

    // tonemap + gamma
    float4 color = float4(tonemap_uc2(hdr.xyz), 1.0);

    // dither
    color = dither(color, In.SV_P.xy, 100);

    return color;
}
