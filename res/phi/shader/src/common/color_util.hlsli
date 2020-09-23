#pragma once

// ---------------------------------------------------------------
// Color / colorspace helpers
// ---------------------------------------------------------------

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

// fast reversible tonemap for TAA or other resolve passes
float3 fast_tonemap(float3 c) { return c * rcp(max3(c.r, c.g, c.b) + 1.0); }
float3 fast_invert_tonemap(float3 c) { return c * rcp(1.0 - max3(c.r, c.g, c.b)); }