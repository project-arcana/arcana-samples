#pragma once

// converts a position in clip space to it's UV on screen
float2 convert_hdc_to_uv(float4 hdc)
{
	// assuming Y+ NDC
	float2 cs = hdc.xy / hdc.w;
	return float2(0.5f * cs.x, -0.5f * cs.y) + 0.5f;
}

// reassembles a HDC position from UV and depth
float4 convert_uv_to_hdc(float2 uv, float depth)
{
	// assuming Y+ NDC
	return float4(uv.x * (2) - 1, uv.y * (-2) + 1, depth, 1.0f);
}

// converts a UV and depth back to a worldspace position
float3 reconstruct_worldspace(float2 uv, float depth, float4x4 inverse_vp)
{
	const float4 hdc = convert_uv_to_hdc(uv, depth);
	const float4 position = mul(inverse_vp, hdc);
	return position.xyz / position.w;
}

// extracts the (world space) camera position from an inverse view matrix
float3 extract_camera_position(float4x4 inverse_view)
{
	return inverse_view._m03_m13_m23;
}

// converts depth as sampled from a (reverse-Z, infinite farplane) depthbuffer to world space units
float linearize_reverse_z(float depth, float nearplane)
{
	return nearplane / depth;
}
