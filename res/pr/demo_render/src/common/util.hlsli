
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

// converts depth as sampled from a depthbuffer to world space units
float linearize_reverse_z(float depth, float nearplane)
{
	return nearplane / depth;
}

// convert a RGBA color to YCoCg
float4 rgba_to_ycocg(float4 val)
{
	return float4(
		dot(val.rgb, float3( 0.25f, 0.50f,  0.25f)),
		dot(val.rgb, float3( 0.50f, 0.00f, -0.50f)),
		dot(val.rgb, float3(-0.25f, 0.50f, -0.25f)),
		val.a);
}

// convert a YCoCg color to RGBA
float4 ycocg_to_rgba(float4 val)
{
	return float4(
		val.x + val.y - val.z,
		val.x + val.z,
		val.x - val.y - val.z,
		val.a);
}

float intersect_aabb(float3 origin, float3 direction, float3 extents)
{
	float3 reciprocal = rcp(direction);
	float3 minimum = ( extents - origin) * reciprocal;
	float3 maximum = (-extents - origin) * reciprocal;
	
	return max(max(min(minimum.x, maximum.x), min(minimum.y, maximum.y)), min(minimum.z, maximum.z));
}

float max3(float x, float y, float z) { return max(x, max(y, z)); }

// fast reversible tonemap for TAA or other resolve passes
float3 fast_tonemap(float3 c) { return c * rcp(max3(c.r, c.g, c.b) + 1.0); }
float3 fast_invert_tonemap(float3 c) { return c * rcp(1.0 - max3(c.r, c.g, c.b)); }
