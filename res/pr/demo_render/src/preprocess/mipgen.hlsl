
Texture2D g_input                  : register(t0, space0);

RWTexture2D<float4> g_output       : register(u0, space0);

[numthreads(8, 8, 1)]
void main_cs(uint2 thread_id : SV_DispatchThreadID)
{
	int3 sample_loc = int3(2 * thread_id.x, 2 * thread_id.y, 0);
	float4 gather_val = 
		g_input.Load(sample_loc, int2(0, 0)) +
		g_input.Load(sample_loc, int2(1, 0)) +
		g_input.Load(sample_loc, int2(0, 1)) +
		g_input.Load(sample_loc, int2(1, 1));
	g_output[thread_id] = 0.25 * gather_val;
}
