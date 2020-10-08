
Texture2D g_input                  : register(t0, space0);
RWTexture2D<float4> g_output       : register(u0, space0);

[numthreads(8, 8, 1)]
void cs_mipgen(uint2 thread_id : SV_DispatchThreadID)
{
	int3 sample_loc = int3(2 * thread_id.x, 2 * thread_id.y, 0);
	float4 gather_val = 
		g_input.Load(sample_loc, int2(0, 0)) +
		g_input.Load(sample_loc, int2(1, 0)) +
		g_input.Load(sample_loc, int2(0, 1)) +
		g_input.Load(sample_loc, int2(1, 1));
	g_output[thread_id] = 0.25 * gather_val;
}

static const float gamma = 2.2;

[numthreads(8, 8, 1)]
void cs_mipgen_gamma(uint2 thread_id : SV_DispatchThreadID)
{
	int3 load_loc = int3(2 * thread_id.x, 2 * thread_id.y, 0);

	float4 value0 = g_input.Load(load_loc, int2(0, 0));
	float4 value1 = g_input.Load(load_loc, int2(1, 0));
	float4 value2 = g_input.Load(load_loc, int2(0, 1));
	float4 value3 = g_input.Load(load_loc, int2(1, 1));

	float4 gather_val;
	gather_val.rgb = pow(value0.rgb, gamma) + pow(value1.rgb, gamma) + pow(value2.rgb, gamma) + pow(value3.rgb, gamma);
	gather_val.a   = value0.a + value1.a + value2.a + value3.a;

	g_output[thread_id] = float4(pow(0.25 * gather_val.rgb, 1.0/gamma), 0.25 * gather_val.a);
}

Texture2DArray g_input_array                  : register(t0, space0);

RWTexture2DArray<float4> g_output_array       : register(u0, space0);

[numthreads(8, 8, 1)]
void cs_mipgen_array(uint3 thread_id : SV_DispatchThreadID)
{
	int4 sample_loc = int4(2 * thread_id.x, 2 * thread_id.y, thread_id.z, 0);
	float4 gather_val = 
		g_input_array.Load(sample_loc, int2(0, 0)) +
		g_input_array.Load(sample_loc, int2(1, 0)) +
		g_input_array.Load(sample_loc, int2(0, 1)) +
		g_input_array.Load(sample_loc, int2(1, 1));
	g_output_array[thread_id] = 0.25 * gather_val;
}
