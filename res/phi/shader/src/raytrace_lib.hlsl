// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
struct HitInfo
{
  float4 colorAndDistance;
};

// Attributes output by the raytracing when hitting a surface,
// here the barycentric coordinates
struct Attributes
{
    float2 bary;
};

struct Ray
{
    float3 origin;
    float3 direction;
};

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
};

// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> gOutput                 : register(u0, space0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH    : register(t0, space0);

ConstantBuffer<camera_constants> gCamData   : register(b0, space0);

// extracts the (world space) camera position from an inverse view matrix
float3 extract_camera_position(float4x4 inverse_view)
{
    return inverse_view._m03_m13_m23;
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
Ray compute_camera_ray(uint2 index, in float3 cameraPosition, in float4x4 vp_inverse)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a world positon.
    float4 world = mul(vp_inverse, float4(screenPos, 1, 1));
    world.xyz /= world.w;

    Ray ray;
    ray.origin = cameraPosition;
    ray.direction = normalize(world.xyz - ray.origin);
    return ray;
}

[shader("raygeneration")] 
void RayGen() 
{
    // Initialize the ray payload
    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);

    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    const Ray ray_info = compute_camera_ray(DispatchRaysIndex().xy, extract_camera_position(gCamData.view_inv), gCamData.vp_inv);

    RayDesc ray_desc;
    ray_desc.Origin = ray_info.origin;
    ray_desc.Direction = ray_info.direction;
    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray_desc.TMin = 0.001;
    ray_desc.TMax = 10000.0;

    // Trace the ray
    TraceRay(SceneBVH, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, ray_desc, payload);
    // Write payload to output UAV
    gOutput[DispatchRaysIndex().xy] = float4(payload.colorAndDistance.rgb, 1.f);
}

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    float ramp = launchIndex.y / dims.y;
    payload.colorAndDistance = float4(0.0f, 0.2f, 0.7f - 0.3f*ramp, -1.0f);
}

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
    float3 barycentrics = 
    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    const float3 A = float3(1, 1, 0);
    const float3 B = float3(0, 1, 1);
    const float3 C = float3(1, 0, 1);

    float3 hitColor = A * barycentrics.x + B * barycentrics.y + C * barycentrics.z;

    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}
