
struct vs_in
{
    uint vid            : SV_VertexID;
};

struct vs_out
{
    float4 SV_P         : SV_POSITION;
    float2 Texcoord     : TEXCOORD;
};

#define TONEMAP_GAMMA 2.224

Texture2D g_texture                             : register(t0, space0);
SamplerState g_sampler                          : register(s0, space0);

vs_out main_vs(vs_in In)
{
    vs_out Out;
    Out.Texcoord = float2((In.vid << 1) & 2, In.vid & 2);
    Out.SV_P = float4(Out.Texcoord * 2.0f + -1.0f, 0.0f, 1.0f);
    Out.SV_P.y = -Out.SV_P.y;
    return Out;
}

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

float4 main_ps(vs_out In) : SV_TARGET
{
    float4 hdr = g_texture.Sample(g_sampler, In.Texcoord);

    float4 color = float4(tonemap_uc2(hdr.xyz), 1.0);

    return color;
}
