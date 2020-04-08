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

StructuredBuffer<mesh_transform> g_mesh_transforms                      : register(t0, space0);
StructuredBuffer<mesh_transform> g_prev_mesh_transforms                 : register(t1, space0);

ConstantBuffer<camera_constants> g_frame_data                           : register(b0, space0);

[[vk::push_constant]] ConstantBuffer<model_constants> g_root_consts     : register(b1, space0);

vs_out_simple main_vs(vs_in v_in)
{
    vs_out_simple Out;

    const float4x4 model_view = g_mesh_transforms[g_root_consts.model_mat_index].mv;
    float4 pos_cam = mul(model_view, float4(v_in.P, 1.0));
    float4 pos_clip = mul(g_frame_data.proj, pos_cam);

    Out.SV_P = pos_clip;
    return Out;
}
