#pragma once

#ifndef M_PI
#define M_PI 3.141592f
#endif

// ---------------------------------------------------------------
// basics
// ---------------------------------------------------------------

float max3(float x, float y, float z) { return max(x, max(y, z)); }

float length2(float3 vec) { return dot(vec, vec); }

float  pow2(float x) { return x*x; }
float2 pow2(float2 x) { return x*x; }
float3 pow2(float3 x) { return x*x; }
float4 pow2(float4 x) { return x*x; }

float  pow3(float  x) { return x*x*x; }
float2 pow3(float2 x) { return x*x*x; }
float3 pow3(float3 x) { return x*x*x; }
float4 pow3(float4 x) { return x*x*x; }

float  pow4(float  x) { float xx = x*x; return xx * xx; }
float2 pow4(float2 x) { float2 xx = x*x; return xx * xx; }
float3 pow4(float3 x) { float3 xx = x*x; return xx * xx; }
float4 pow4(float4 x) { float4 xx = x*x; return xx * xx; }

float  pow5(float x) { float xx = x*x; return xx * xx * x; }
float2 pow5(float2 x) { float2 xx = x*x; return xx * xx * x; }
float3 pow5(float3 x) { float3 xx = x*x; return xx * xx * x; }
float4 pow5(float4 x) { float4 xx = x*x; return xx * xx * x; }

float  pow6(float  x) { float xx = x*x; return xx * xx * xx; }
float2 pow6(float2 x) { float2 xx = x*x; return xx * xx * xx; }
float3 pow6(float3 x) { float3 xx = x*x; return xx * xx * xx; }
float4 pow6(float4 x) { float4 xx = x*x; return xx * xx * xx; }

uint reverse_bits_32( uint bits )
{
#if SM5_PROFILE
	return reversebits( bits );
#else
	bits = ( bits << 16) | ( bits >> 16);
	bits = ( (bits & 0x00ff00ff) << 8 ) | ( (bits & 0xff00ff00) >> 8 );
	bits = ( (bits & 0x0f0f0f0f) << 4 ) | ( (bits & 0xf0f0f0f0) >> 4 );
	bits = ( (bits & 0x33333333) << 2 ) | ( (bits & 0xcccccccc) >> 2 );
	bits = ( (bits & 0x55555555) << 1 ) | ( (bits & 0xaaaaaaaa) >> 1 );
	return bits;
#endif
}

uint float_to_fixed_point(float x, uint num_decimal_bits)
{
	uint scale = 1 << num_decimal_bits;
	return x * scale;
}

float fixed_point_to_float(uint x, uint num_decimal_bits)
{
	float scale = 1 << num_decimal_bits;
	return x / scale;
}

