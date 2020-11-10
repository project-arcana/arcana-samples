#pragma once

#include "random.hlsli"
#include "math.hlsli"
#include "space_conversion.hlsli"

// Retrieve world position of the current ray hit
float3 get_rayhit_world_position()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

uint get_raygen_linear_dispatch_index()
{
    return DispatchRaysIndex().y * DispatchRaysDimensions().x + DispatchRaysIndex().x;
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
// Camera Models / ray info computation
// ---------------------------------------------------------------


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

void ray_rng_initialize(inout RNGState rng, uint frame_index)
{
    rng_initialize(rng, get_raygen_linear_dispatch_index(), frame_index);
}

float2 get_subpixel_jitter(inout RNGState rng)
{
    float2 r = rng_f2(rng);
    return r - .5f;
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid
RayDesc compute_primary_ray(uint2 index, float3 cameraPosition, float4x4 vp_inverse, float2 jitter)
{
    float3 world = ray_dispatch_index_to_worldspace(index, 1.f, jitter, vp_inverse);

    RayDesc ray;
    ray.Origin = cameraPosition;
    ray.Direction = normalize(world - ray.Origin);
    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray.TMin = 0.0;
    ray.TMax = 1.0e27;
    return ray;
}
