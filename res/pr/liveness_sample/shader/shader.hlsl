struct vs_in
{
    float3 P : POSITION;
    float3 N : NORMAL;
    float2 Texcoord : TEXCOORD;
    float4 Tangent : TANGENT;
};

struct vs_out
{
    float4 SV_P : SV_POSITION;
    float3x3 TBN : VO_TBN;
    float3 WorldPos : VO_WP;
    float2 Texcoord : VO_UV;
};

struct CameraConstants
{
    float4x4 view_proj;
    float runtime;
};

struct ModelConstants
{
    float4x4 model;
};

SamplerState g_sampler                              : register(s0, space1);

Texture2D<float4> g_albedo                          : register(t0, space1);
Texture2D<float4> g_normal                          : register(t1, space1);

ConstantBuffer<CameraConstants> g_FrameData         : register(b0, space0);
ConstantBuffer<ModelConstants> g_ModelData          : register(b0, space1);


vs_out mainVS(vs_in In)
{
    vs_out Out;

    const float4x4 mvp = mul(g_FrameData.view_proj, g_ModelData.model);
    Out.SV_P = mul(mvp, float4(In.P, 1.0));
    Out.WorldPos = mul(g_ModelData.model, float4(In.P, 1.0)).xyz;
    float3 N = normalize(mul((float3x3)g_ModelData.model, In.N));
    Out.Texcoord = In.Texcoord;
    
    float3 tangent = mul((float3x3)g_ModelData.model, In.Tangent.xyz);
    // gram-schmidt re-orthogonalization
    tangent = normalize(tangent) - dot(normalize(tangent), In.N) * In.N;

    float3 bitangent = mul((float3x3)g_ModelData.model, cross(tangent, In.N));
    Out.TBN = transpose(float3x3(normalize(tangent), normalize(bitangent), normalize(N))); 

    return Out;
}

float get_pointlight_contrib(float3 normal, float3 worldpos, float3 lightpos)
{
    const float3 L = normalize(lightpos - worldpos);
    return saturate(dot(normal, L));
}

float4 mainPS(vs_out In) : SV_TARGET
{
    float3 N = g_normal.Sample(g_sampler, In.Texcoord).rgb;
    N = N * 2.f - 1.f;
    N = normalize(mul(In.TBN, N));
    
    const float3 albedo = g_albedo.Sample(g_sampler, In.Texcoord).rgb;

    float light_intensity = get_pointlight_contrib(N, In.WorldPos, float3(-2, 2, 3));
    light_intensity += get_pointlight_contrib(N, In.WorldPos, float3(3, 2, -2));
    light_intensity += get_pointlight_contrib(N, In.WorldPos, float3(3, 3, 3));

    light_intensity = max(light_intensity, .15);

    return float4(albedo * (light_intensity), 1.0);
} 
