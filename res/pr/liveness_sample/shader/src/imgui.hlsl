
cbuffer vertexBuffer : register(b0, space0)
{
    float4x4 proj;
};

struct VS_INPUT
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};

struct vert_globals
{
    float4x4 proj;
};

ConstantBuffer<vert_globals> g_vert_globals        : register(b0, space0);

SamplerState g_sampler : register(s0, space0);
Texture2D g_texture : register(t0, space0);

PS_INPUT main_vs(VS_INPUT input)
{
    PS_INPUT output;
    output.pos = mul(g_vert_globals.proj, float4(input.pos.xy, 0.f, 1.f));
    output.col = input.col;
    output.uv = input.uv;
    return output;
}

float4 main_ps(PS_INPUT input) : SV_TARGET
{
    float4 res = input.col * g_texture.Sample(g_sampler, input.uv);
    return res;
}
