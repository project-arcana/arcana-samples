
struct vs_in
{
    uint vid            : SV_VertexID;
};

struct vs_out
{
    float4 SV_P         : SV_POSITION;
    float2 Texcoord     : TEXCOORD;
};

Texture2D<float4> g_texture                     : register(t0, space0);
SamplerState g_sampler                          : register(s0, space0);

vs_out main_vs(vs_in In)
{
    vs_out Out;
    Out.Texcoord = float2((In.vid << 1) & 2, In.vid & 2);
    Out.SV_P = float4(Out.Texcoord * 2.0f + -1.0f, 0.0f, 1.0f);
    Out.SV_P.y = -Out.SV_P.y;
    return Out;
}

float4 main_ps(vs_out In) : SV_TARGET
{
    return g_texture.Sample(g_sampler, In.Texcoord);
}
