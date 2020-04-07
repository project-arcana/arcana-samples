#include "common/mesh_vs_inout.hlsl"
#include "common/cam_constants.hlsl"

vs_out calculate_vs_out(vs_in v_in, float4x4 model, float4x4 mvp)
{
    vs_out Out;
    Out.SV_P = mul(mvp, float4(v_in.P, 1.0));
    Out.WorldPos = mul(model, float4(v_in.P, 1.0)).xyz;
    float3 N = normalize(mul((float3x3)model, v_in.N));
    Out.Texcoord = v_in.Texcoord;
    
    float3 tangent = mul((float3x3)model, v_in.Tangent.xyz);
    // gram-schmidt re-orthogonalization
    tangent = normalize(tangent) - dot(normalize(tangent), v_in.N) * v_in.N;

    float3 bitangent = mul((float3x3)model, cross(tangent, v_in.N));
    Out.TBN = transpose(float3x3(normalize(tangent), normalize(bitangent), normalize(N))); 
    return Out;
}

struct model_constants
{
    uint model_mat_index;
};

StructuredBuffer<float4x4> g_model_matrices         : register(t0, space0);

ConstantBuffer<camera_constants> g_frame_data       : register(b0, space0);

[[vk::push_constant]] ConstantBuffer<model_constants> g_model_data        : register(b1, space0);

vs_out main_vs(vs_in v_in)
{
    const float4x4 model = g_model_matrices[g_model_data.model_mat_index];
    const float4x4 mvp = mul(g_frame_data.view_proj, model);
    return calculate_vs_out(v_in, model, mvp);
}
