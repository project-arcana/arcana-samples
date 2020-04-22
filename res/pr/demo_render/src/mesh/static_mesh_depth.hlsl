#include "common/mesh_vs_inout.hlsli"
#include "common/cam_constants.hlsli"
#include "common/util.hlsli"

struct vs_out_simple
{
    float4 SV_P : SV_POSITION;
    float4 CleanClip : VO_CC;
    float4 CleanPrevClip : VO_CPC;
};

struct model_constants
{
    uint model_mat_index;
};

StructuredBuffer<mesh_transform> g_mesh_transforms                      : register(t0, space0);

ConstantBuffer<camera_constants> g_frame_data                           : register(b0, space0);

[[vk::push_constant]] ConstantBuffer<model_constants> g_root_consts     : register(b1, space0);

vs_out_simple main_vs(vs_in v_in)
{
    vs_out_simple Out;

    // model matrices from SB
    const float4x4 model = g_mesh_transforms[g_root_consts.model_mat_index].model;
    const float4x4 prev_model = g_mesh_transforms[g_root_consts.model_mat_index].prev_model;

    // prev and current world position
    float4 pos_world = mul(model, float4(v_in.P, 1.0));
    float4 pos_prev_world = mul(prev_model, float4(v_in.P, 1.0));

    // current high-precision (jittered) clip position
    float4 pos_cam = mul(g_frame_data.view, pos_world);
    float4 pos_clip = mul(g_frame_data.proj, pos_cam);
    Out.SV_P = pos_clip;

    // current and previous clean (unjittered) clip positions
    Out.CleanClip = mul(g_frame_data.clean_vp, pos_world);
    Out.CleanPrevClip = mul(g_frame_data.prev_clean_vp, pos_prev_world);
    return Out;
}

float2 main_ps(vs_out_simple p_in) : SV_TARGET
{
    // while possible in HLSL, using p_in.SV_P would be wrong here as dxc translates it to FragCoord
    float2 uv_current = convert_hdc_to_uv(p_in.CleanClip);
    float2 uv_prev = convert_hdc_to_uv(p_in.CleanPrevClip);
    return uv_current - uv_prev;
}
