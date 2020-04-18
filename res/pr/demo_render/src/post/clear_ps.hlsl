#include "common/fullscreen_vs_inout.hlsli"

struct clearval_t
{
    float rgb;
    float alpha;
};

[[vk::push_constant]] ConstantBuffer<clearval_t> g_clearval     : register(b1, space0);


float4 main_ps(vs_out In) : SV_TARGET
{
    return float4(g_clearval.rgb.xxx, g_clearval.alpha);
}
