
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
