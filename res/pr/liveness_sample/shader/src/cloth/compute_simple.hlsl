
struct Particle 
{
	float4 pos;
	float4 vel;
};

RWStructuredBuffer<Particle> g_particles_out : register(u0, space0);

[numthreads(10, 10, 1)]
void main_cs(uint3 id : SV_DispatchThreadID) 
{
	uint index = id.y * 60 + id.x;
	g_particles_out[index].pos = float4(1, 2, 3, 5);
}