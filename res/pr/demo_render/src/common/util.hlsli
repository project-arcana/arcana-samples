
// converts a position in clip space to it's UV on screen
float2 clipspace_to_uv(float4 pos_clipspace)
{
	float2 cs = pos_clipspace.xy / pos_clipspace.w;
	return float2(0.5f * cs.x, -0.5f * cs.y) + 0.5f;
}

// converts a UV and depth back to a clipspace position
float3 reconstruct_clipspace(float2 uv, float depth, float4x4 inverse_vp)
{
	float4 position = float4(uv.x * (2) - 1, uv.y * (-2) + 1, depth, 1.0f);
	position = mul(inverse_vp, position);
	return position.xyz / position.w;
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
