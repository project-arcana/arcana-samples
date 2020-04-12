#include "common/mesh_vs_inout.hlsl"
#include "common/cam_constants.hlsl"

struct model_constants
{
    uint model_mat_index;
};

StructuredBuffer<mesh_transform> g_mesh_transforms                          : register(t0, space0);

ConstantBuffer<camera_constants> g_frame_data                               : register(b0, space0);

[[vk::push_constant]] ConstantBuffer<model_constants> g_root_consts         : register(b1, space0);

vs_out main_vs(vs_in v_in)
{
    vs_out Out;

    const float4x4 model = g_mesh_transforms[g_root_consts.model_mat_index].model;
    float4 pos_world = mul(model, float4(v_in.P, 1.0));
    float4 pos_cam = mul(g_frame_data.view, pos_world);
    // non-premultiplied projection for optimal depth precision (Upchurch and Desbrun 2012)
    float4 pos_clip = mul(g_frame_data.proj, pos_cam);
    
    Out.SV_P = pos_clip;
    Out.WorldPos = pos_world.xyz;

    float3 N = normalize(mul((float3x3)model, v_in.N));
    Out.Texcoord = v_in.Texcoord;
    
    float3 tangent = mul((float3x3)model, v_in.Tangent.xyz);
    // gram-schmidt re-orthogonalization
    tangent = normalize(tangent) - dot(normalize(tangent), v_in.N) * v_in.N;

    float3 bitangent = mul((float3x3)model, cross(tangent, v_in.N));
    Out.TBN = transpose(float3x3(normalize(tangent), normalize(bitangent), normalize(N))); 
    return Out;
}
