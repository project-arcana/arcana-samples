#include "common/fullscreen_vs_inout.hlsli"

Texture2D g_texture                             : register(t0, space0);
SamplerState g_sampler                          : register(s0, space0);

float4 main_ps(vs_out In) : SV_TARGET
{
    return float4(g_texture.Sample(g_sampler, In.Texcoord).rgb, 1.0);
}
