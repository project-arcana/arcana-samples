struct vs_in
{
    float3 P : POSITION;
    float3 N : NORMAL;
    float2 Texcoord : TEXCOORD;
};

struct vs_out
{
    float4 SV_P : SV_POSITION;
    float3 N : NORMAL;
    float3 WorldPos : WS_POS;
    float2 Texcoord : TEXCOORD;
};

struct CameraConstants
{
    float4x4 view_proj;
};

struct ModelConstants
{
    float4x4 model;
};

Texture2D<float4> g_albedo                          : register(t0, space1);

ConstantBuffer<CameraConstants> g_FrameData         : register(b0, space0);
ConstantBuffer<ModelConstants> g_ModelData          : register(b0, space1);

SamplerState g_sampler                              : register(s0, space0);

vs_out mainVS(vs_in In)
{
    vs_out Out;

    const float4x4 mvp = mul(g_FrameData.view_proj, g_ModelData.model);
    Out.SV_P = mul(mvp, float4(In.P, 1.0));
    Out.WorldPos = mul(g_ModelData.model, float4(In.P, 1.0)).xyz;

    float3 worldNormal = mul((float3x3)g_ModelData.model, In.N);
    Out.N = normalize(worldNormal);

    Out.Texcoord = In.Texcoord;
    return Out;
}

float get_pointlight_contrib(float3 normal, float3 worldpos, float3 lightpos)
{
    const float3 L = normalize(lightpos - worldpos);
    return saturate(dot(normal, L));
}

float4 mainPS(vs_out In) : SV_TARGET
{
    const float3 N = normalize(In.N);
    const float3 albedo = g_albedo.Sample(g_sampler, In.Texcoord).rgb;

    float light_intensity = get_pointlight_contrib(N, In.WorldPos, float3(-2, 2, 3));
    light_intensity += get_pointlight_contrib(N, In.WorldPos, float3(3, 2, -2));
    light_intensity += get_pointlight_contrib(N, In.WorldPos, float3(3, 3, 3));

    light_intensity = max(light_intensity, .15);

    return float4(albedo * light_intensity, 1.0);
} 
