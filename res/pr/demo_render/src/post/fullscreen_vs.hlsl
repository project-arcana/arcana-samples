#include "common/fullscreen_vs_inout.hlsl"

vs_out main_vs(vs_in In)
{
    vs_out Out;
    Out.Texcoord = float2((In.vid << 1) & 2, In.vid & 2);
    Out.SV_P = float4(Out.Texcoord * 2.0f + -1.0f, 0.0f, 1.0f);
    Out.SV_P.y = -Out.SV_P.y;
    return Out;
}
