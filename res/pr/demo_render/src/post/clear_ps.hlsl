#include "common/fullscreen_vs_inout.hlsli"

float4 main_ps(vs_out In) : SV_TARGET
{
    return float4(In.Texcoord.x, In.Texcoord.y, 1.f, 1.f);
}
