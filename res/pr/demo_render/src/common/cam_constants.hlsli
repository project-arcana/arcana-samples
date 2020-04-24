
struct camera_constants
{
    float4x4 proj; // jittered
    float4x4 proj_inv;

    float4x4 view;
    float4x4 view_inv;
    
    float4x4 vp; // jittered proj
    float4x4 vp_inv;

    float4x4 clean_vp;
    float4x4 clean_vp_inv;
    
    float4x4 prev_clean_vp;
    float4x4 prev_clean_vp_inv;

    float3 cam_pos;
    float runtime;
};

struct mesh_transform
{
    float4x4 model;
    float4x4 prev_model;
};
