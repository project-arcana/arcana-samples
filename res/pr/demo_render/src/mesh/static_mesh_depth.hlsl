#include "common/mesh_vs_inout.hlsl"
#include "common/cam_constants.hlsl"

struct vs_out_simple
{
    float4 SV_P : SV_POSITION;
};

struct model_constants
{
    uint model_mat_index;
};

StructuredBuffer<float4x4> g_model_matrices         : register(t0, space0);

ConstantBuffer<camera_constants> g_frame_data       : register(b0, space0);

[[vk::push_constant]] ConstantBuffer<model_constants> g_model_data        : register(b1, space0);

vs_out_simple main_vs(vs_in v_in)
{
    const float4x4 model = g_model_matrices[g_model_data.model_mat_index];
    const float4x4 mvp = mul(g_frame_data.view_proj, model);

    vs_out_simple Out;
    Out.SV_P = mul(mvp, float4(v_in.P, 1.0));
    return Out;
}

void main_ps(vs_out_simple p_in) : SV_TARGET
{
}
