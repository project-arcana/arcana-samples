// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Converts equirectangular (lat-long) projection texture into a proper cubemap.

static const float PI = 3.141592;
static const float TwoPI = 2 * PI;

Texture2D g_input                       : register(t0, space0);
RWTexture2DArray<float4> g_output       : register(u0, space0);

SamplerState g_sampler                  : register(s0, space0);

// Calculate normalized sampling direction vector based on current fragment coordinates.
// This is essentially "inverse-sampling": we reconstruct what the sampling vector would be if we wanted it to "hit"
// this particular fragment in a cubemap.
float3 get_sampling_vector(uint3 thread_id)
{
	float outputWidth, outputHeight, outputDepth;
	g_output.GetDimensions(outputWidth, outputHeight, outputDepth);

    float2 st = thread_id.xy/float2(outputWidth, outputHeight);
    float2 uv = 2.0 * float2(st.x, 1.0-st.y) - float2(1.0, 1.0);

	// Select vector based on cubemap face index.
	float3 ret;
	switch(thread_id.z)
	{
	case 0: ret = float3(1.0,  uv.y, -uv.x); break;
	case 1: ret = float3(-1.0, uv.y,  uv.x); break;
	case 2: ret = float3(uv.x, 1.0, -uv.y); break;
	case 3: ret = float3(uv.x, -1.0, uv.y); break;
	case 4: ret = float3(uv.x, uv.y, 1.0); break;
	case 5: ret = float3(-uv.x, uv.y, -1.0); break;
	}
    return normalize(ret);
}

[numthreads(32, 32, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
	float3 v = get_sampling_vector(thread_id);
	
	// Convert Cartesian direction vector to spherical coordinates.
	float phi   = atan2(v.z, v.x);
	float theta = acos(v.y);

	// Sample equirectangular texture.
	float4 color = g_input.SampleLevel(g_sampler, float2(phi/TwoPI, theta/PI), 0);

	// Write out color to output cubemap.
	g_output[thread_id] = color;
}