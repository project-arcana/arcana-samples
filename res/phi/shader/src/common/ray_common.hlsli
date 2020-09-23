#pragma once

#include "random.hlsli"
#include "math.hlsli"
#include "space_conversion.hlsli"

// Retrieve world position of the current ray hit
float3 get_world_hit_position()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

// ---------------------------------------------------------------
// Vertex and Index information retrieval
// from: D3D12 DXR Samples (MIT)
// ---------------------------------------------------------------

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

// ---------------------------------------------------------------
// Camera Models / Ray info computation
// ---------------------------------------------------------------

struct Ray
{
    float3 origin;
    float3 direction;
};

// computes the homogenous device coordinates (HDC) of the position at the given pixel
float4 ray_dispatch_index_to_hdc(uint2 dispatch_index, float depth, float2 jitter)
{
    float2 xy = dispatch_index + 0.5f + jitter; // center in the middle of the pixel
    return convert_uv_to_hdc(xy / DispatchRaysDimensions().xy, depth);
}

float3 ray_dispatch_index_to_worldspace(uint2 dispatch_index, float depth, float2 jitter, float4x4 inverse_vp)
{
	const float4 hdc = ray_dispatch_index_to_hdc(dispatch_index, depth, jitter);
	const float4 position = mul(inverse_vp, hdc);
	return position.xyz / position.w;
}

float2 get_subpixel_jitter(uint2 dispatch_index, uint sample_index)
{
    uint seed = rng_tea(dispatch_index.y * DispatchRaysDimensions().x + dispatch_index.x, sample_index);
    float r1 = rng_lcg_float(seed);
    float r2 = rng_lcg_float(seed);
    return dispatch_index == 0 ? 0.f : float2(r1 - .5f, r2 - .5f);
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid
Ray compute_simple_camera_ray(uint2 index, float3 cameraPosition, float4x4 vp_inverse)
{
    float3 world = ray_dispatch_index_to_worldspace(index, 1.f, 0.f, vp_inverse);

    Ray ray;
    ray.origin = cameraPosition;
    ray.direction = normalize(world - ray.origin);
    return ray;
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid
// with subpixel jitter based on the sample index (in a sequence of samples of the same scene)
Ray compute_jittered_camera_ray(uint2 index, uint sample_index, float3 cameraPosition, float4x4 vp_inverse)
{
    float3 world = ray_dispatch_index_to_worldspace(index, 1.f, get_subpixel_jitter(index, sample_index), vp_inverse);

    Ray ray;
    ray.origin = cameraPosition;
    ray.direction = normalize(world - ray.origin);
    return ray;
}
