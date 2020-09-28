#pragma once

// ---------------------------------------------------------------
// Color / colorspace helpers
// ---------------------------------------------------------------

// RGBA <-> YCoCg color conversion
float4 rgba_to_ycocg(float4 val)
{
	return float4(
		dot(val.rgb, float3( 0.25f, 0.50f,  0.25f)),
		dot(val.rgb, float3( 0.50f, 0.00f, -0.50f)),
		dot(val.rgb, float3(-0.25f, 0.50f, -0.25f)),
		val.a);
}

float4 ycocg_to_rgba(float4 val)
{
	return float4(
		val.x + val.y - val.z,
		val.x + val.z,
		val.x - val.y - val.z,
		val.a);
}

// fast reversible tonemap for TAA or other resolve passes
float3 fast_tonemap(float3 lin_c) { return lin_c * rcp(max3(lin_c.r, lin_c.g, lin_c.b) + 1.0); }
float3 fast_invert_tonemap(float3 map_c) { return map_c * rcp(1.0 - max3(map_c.r, map_c.g, map_c.b)); }

float get_luminance(float3 c) { return dot(c, float3(.3f, .59f, .11f)); }