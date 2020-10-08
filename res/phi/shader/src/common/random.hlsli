#pragma once

// ---------------------------------------------------------------
// RNG helpers
// adapted from: nvpro vk denoising samples
// ---------------------------------------------------------------

// Generate a random unsigned int from two unsigned int values, using 8 pairs
// of rounds of the Tiny Encryption Algorithm (TEA). See Zafar, Olano, and Curtis,
// "GPU Random Numbers via the Tiny Encryption Algorithm"
uint random_tea(uint seed0, uint seed1)
{
    uint2 val = uint2(seed0, seed1);
    uint sum = 0;

    for (uint n = 0; n < 8; ++n)
    {
        sum += 0x9e3779b9;
        val.x += ((val.y << 4) + 0xa341316c) ^ (val.y + sum) ^ ((val.y >> 5) + 0xc8013ea4);
        val.y += ((val.x << 4) + 0xad90777d) ^ (val.x + sum) ^ ((val.x >> 5) + 0x7e95761e);
    }

    return val.x;
}

// Generate a random unsigned int in [0, 2^24) given the previous RNG state
// using the Numerical Recipes linear congruential generator
uint random_lcg(inout uint prev)
{
    const uint LCG_Scale = 1664525u; // = 48271;
    const uint LCG_Bias = 1013904223u; // = 0;
    prev = (LCG_Scale * prev + LCG_Bias);
    return prev & 0x00FFFFFF;
}

// Generate a random float in [0, 1) given the previous RNG state
float random_lcg_float(inout uint prev)
{
  return (float(random_lcg(prev)) / float(0x01000000));
}

// http://burtleburtle.net/bob/hash/integer.html
// Bob Jenkins integer hashing function in 6 shifts.
uint integer_hash(uint a)
{
	a = (a + 0x7ed55d16) + (a << 12);
	a = (a ^ 0xc761c23c) ^ (a >> 19);
	a = (a + 0x165667b1) + (a << 5);
	a = (a + 0xd3a2646c) ^ (a << 9);
	a = (a + 0xfd7046c5) + (a << 3);
	a = (a ^ 0xb55a4f09) ^ (a >> 16);
	return a;
}

float halton_sequence(uint index, uint base)
{
	const float base_inv = 1.0 / base;
	float r = 0.0;
	float f = 1.0;

	while (index > 0)
	{
		f *= base_inv;
		r += f * (index % base);
		index /= base;
	}

	return r;
}

uint wang_hash(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

float wang_float(uint hash)
{
    return hash / float(0x7FFFFFFF) / 2.0;
}

float3 dither(in float3 color, float2 screen_pixel, int divisor) {
    uint seed = uint(screen_pixel.x) + uint(screen_pixel.y) * 8096;
    float r = wang_float(wang_hash(seed * 3 + 0));
    float g = wang_float(wang_hash(seed * 3 + 1));
    float b = wang_float(wang_hash(seed * 3 + 2));
    float3 random = float3(r, g, b);

    return color + ((random - .5) / divisor);
}

// ---------------------------------------------------------------
// Sampling helpers
// ---------------------------------------------------------------

// around +Z
float3 sample_hemisphere(inout uint seed, float3 x, float3 y, float3 z)
{
  float r1 = random_lcg_float(seed);
  float r2 = random_lcg_float(seed);
  float sq = sqrt(1.0 - r2);

  float3 direction = float3(cos(2 * 3.141592 * r1) * sq, sin(2 * 3.141592 * r1) * sq, sqrt(r2));
  direction = direction.x * x + direction.y * y + direction.z * z;

  return direction;
}

struct RNGState
{
	uint state;
};

void rng_initialize(inout RNGState rng, uint seed_pos, uint seed_time)
{
	rng.state = random_tea(seed_pos, seed_time);
}

float rng_f1(inout RNGState rng)
{
	return random_lcg_float(rng.state);
}

float2 rng_f2(inout RNGState rng)
{
	float2 res;
	res.x = random_lcg_float(rng.state);
	res.y = random_lcg_float(rng.state);
	return res;
}

float3 rng_f3(inout RNGState rng)
{
	float3 res;
	res.x = random_lcg_float(rng.state);
	res.y = random_lcg_float(rng.state);
	res.z = random_lcg_float(rng.state);
	return res;
}

float4 rng_f4(inout RNGState rng)
{
	float4 res;
	res.x = random_lcg_float(rng.state);
	res.y = random_lcg_float(rng.state);
	res.z = random_lcg_float(rng.state);
	res.w = random_lcg_float(rng.state);
	return res;
}
