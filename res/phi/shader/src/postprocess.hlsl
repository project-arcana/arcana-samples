#include "common/tonemappers.hlsli"

Texture2D g_texture                             : register(t0, space0);
SamplerState g_sampler                          : register(s0, space0);

float4 main_ps(
    in noperspective float2 Texcoord : TEXCOORD0
) : SV_TARGET
{
    float4 hdr = g_texture.Sample(g_sampler, Texcoord);

    // tonemap
    float3 color = tonemap_aces_fitted(hdr.rgb);

    // gamma
    color = pow(color, 1.0f / 2.224f);

    return float4(color, 1.f);
}
