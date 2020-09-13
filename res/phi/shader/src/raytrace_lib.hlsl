// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
struct CustomRayPayload
{
    float4 colorAndDistance;
    uint currentRecursion;
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

struct Vertex
{
    float3 P;
    float3 N;
    float2 Texcoord;
    float4 Tangent;
};

#define MAX_RAY_RECURSION_DEPTH 4

// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> gOutput                 : register(u0, space0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure gSceneBVH    : register(t0, space0);

ConstantBuffer<camera_constants> gCamData   : register(b0, space0);

// Mesh resources
ByteAddressBuffer gMeshIndices              : register(t0, space1);
StructuredBuffer<Vertex> gMeshVertices      : register(t1, space1);

// extracts the (world space) camera position from an inverse view matrix
float3 extract_camera_position(float4x4 inverse_view)
{
    return inverse_view._m03_m13_m23;
}

// Retrieve hit world position.
float3 get_world_hit_position()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

// Load three 16 bit indices from a byte addressed buffer.
uint3 load_3x16bit_indices(uint offsetBytes, ByteAddressBuffer indexBuffer)
{
    uint3 indices;

    // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
    // Since we need to read three 16 bit indices: { 0, 1, 2 } 
    // aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
    // we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
    // based on first index's offsetBytes being aligned at the 4 byte boundary or not:
    //  Aligned:     { 0 1 | 2 - }
    //  Not aligned: { - 0 | 1 2 }
    const uint dwordAlignedOffset = offsetBytes & ~3;    
    const uint2 four16BitIndices = indexBuffer.Load2(dwordAlignedOffset);
 
    // Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else // Not aligned: { - 0 | 1 2 } => retrieve last three 16bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

// Retrieve attribute at a hit position interpolated from vertex attributes using the hit's barycentrics.
float3 interpolate_hit_attribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
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

float4 TraceRadianceRay(in Ray ray, in uint currentRayRecursionDepth)
{
    if (currentRayRecursionDepth >= MAX_RAY_RECURSION_DEPTH)
    {
        return float4(0, 0, 0, 0);
    }

    // Set the ray's extents.
    RayDesc rayDesc;
    rayDesc.Origin = ray.origin;
    rayDesc.Direction = ray.direction;
    // Set TMin to a zero value to avoid aliasing artifacts along contact areas.
    // Note: make sure to enable face culling so as to avoid surface face fighting.
    rayDesc.TMin = 0;
    rayDesc.TMax = 10000;
    CustomRayPayload rayPayload = { float4(0, 0, 0, 0), currentRayRecursionDepth + 1 };
    // TraceRay(gSceneBVH,
    //     RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
    //     0xFF,
    //     0,// TraceRayParameters::HitGroup::Offset[RayType::Radiance],
    //     1,//TraceRayParameters::HitGroup::GeometryStride,
    //     0,//TraceRayParameters::MissShader::Offset[RayType::Radiance],
    //     rayDesc, rayPayload);

    return rayPayload.colorAndDistance;
}

[shader("raygeneration")] 
void EPrimaryRayGen() 
{
    // Initialize the ray payload
    CustomRayPayload payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);
    payload.currentRecursion = 0;

    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    const Ray ray_info = compute_camera_ray(DispatchRaysIndex().xy, extract_camera_position(gCamData.view_inv), gCamData.vp_inv);

    RayDesc ray_desc;
    ray_desc.Origin = ray_info.origin;
    ray_desc.Direction = ray_info.direction;
    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray_desc.TMin = 0.001;
    ray_desc.TMax = 1000.0;

    // Trace the ray
    // params after flag (2-5): visibility mask, hitgroup offset, multiplier for geometry index (BLAS element index), miss shader index
    TraceRay(gSceneBVH, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, ray_desc, payload);
    // Write payload to output UAV
    gOutput[DispatchRaysIndex().xy] = float4(payload.colorAndDistance.rgb, 1.f);
}

[shader("miss")]
void EMiss(inout CustomRayPayload payload : SV_RayPayload)
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    float ramp = launchIndex.y / dims.y;
    payload.colorAndDistance = float4(0.0f, 0.2f, 0.7f - 0.3f*ramp, -1.0f);
}

[shader("closesthit")] 
void EBarycentricClosestHit(inout CustomRayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
    float3 hitPosition = get_world_hit_position();

    // Get the base index of the triangle's first 16 bit index.
    uint indexSizeInBytes = 2;
    uint indicesPerTriangle = 3;
    uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
    uint baseIndex = PrimitiveIndex() * triangleIndexStride;

    // Load up 3 16 bit indices for the triangle.
    const uint3 indices = load_3x16bit_indices(baseIndex, gMeshIndices);
        
    // Retrieve corresponding vertex normals for the triangle vertices.
    float3 vertexNormals[3] = { 
        gMeshVertices[indices[0]].N, 
        gMeshVertices[indices[1]].N, 
        gMeshVertices[indices[2]].N 
    };

    // Compute the triangle's normal.
    float3 triangleNormal = interpolate_hit_attribute(vertexNormals, attrib);

    Ray reflectionRay = {hitPosition, reflect(WorldRayDirection(), triangleNormal) };
    float4 reflectionColor = TraceRadianceRay(reflectionRay, payload.currentRecursion);

    payload.colorAndDistance = float4(reflectionRay.direction * reflectionColor.rgb, RayTCurrent());
}

[shader("closesthit")] 
void EClosestHitFlatColor(inout CustomRayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
    payload.colorAndDistance = float4(1 * saturate(RayTCurrent() / 50.f), .3f, .7f, RayTCurrent());
}

[shader("closesthit")] 
void EClosestHitErrorState(inout CustomRayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
    payload.colorAndDistance = float4(0, 1 * (1.0 - saturate(RayTCurrent() / 20.f)), 0, RayTCurrent());
}
