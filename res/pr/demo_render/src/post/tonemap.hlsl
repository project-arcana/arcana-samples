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

float2 queryInverseResolution(float nominator)
{
    float width, height;
    g_texture.GetDimensions(width, height);
    return float2(
        nominator / width,
        nominator / height
    );
}

float3 sample_with_sharpen(float2 uv)
{
    const float val0 = 2.f;
    const float val1 = -0.125f;

    const float2 invResolution = queryInverseResolution(1.6f);
    const float dx = invResolution.x;
    const float dy = invResolution.y;

    const float3 color[9] = 
	{
		g_texture.Sample(g_sampler, uv + float2(-dx, -dy)).rgb * val1,
		g_texture.Sample(g_sampler, uv + float2(-dx,   0)).rgb * val1,
		g_texture.Sample(g_sampler, uv + float2(-dx,  dy)).rgb * val1,
		g_texture.Sample(g_sampler, uv + float2(  0, -dy)).rgb * val1,
		g_texture.Sample(g_sampler, uv + float2(  0,   0)).rgb * val0,
		g_texture.Sample(g_sampler, uv + float2(  0,  dy)).rgb * val1,
		g_texture.Sample(g_sampler, uv + float2( dx, -dy)).rgb * val1,
		g_texture.Sample(g_sampler, uv + float2( dx,   0)).rgb * val1,
		g_texture.Sample(g_sampler, uv + float2( dx,  dy)).rgb * val1,
	};
	return color[0] + color[1] + color[2] + color[3] + color[4] + color[5] + color[6] + color[7] + color[8];
}

float4 main_ps(vs_out In) : SV_TARGET
{
    // sharpen
    float3 hdr = sample_with_sharpen(In.Texcoord);

    // tonemap
    float3 color = tonemap_aces_fitted(hdr);

    // gamma
    color = pow(color, 1.0f / 2.224f);

    // dither
    color = dither(color, In.SV_P.xy, 100);

    return float4(color, 1.f);
}
