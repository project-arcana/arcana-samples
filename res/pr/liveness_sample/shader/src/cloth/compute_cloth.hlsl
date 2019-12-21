
struct Particle 
{
	float4 pos;
	float4 vel;
	float4 uv;
	float4 normal;
	float pinned;
};

struct physics_constants_t 
{
	float deltaT;
	float particleMass;
	float springStiffness;
	float damping;
	float restDistH;
	float restDistV;
	float restDistD;
	float sphereRadius;
	float4 spherePos;
	float4 gravity;
	int2 particleCount;
};

struct root_const_t
{
    uint calculate_normals;
};

StructuredBuffer<Particle> g_particles_in : register(t0, space0);

RWStructuredBuffer<Particle> g_particles_out : register(u0, space0);

ConstantBuffer<physics_constants_t> g_physics_params : register(b0, space0);

[[vk::push_constant]] ConstantBuffer<root_const_t> g_root_consts : register(b1, space0);

float3 springForce(float3 p0, float3 p1, float restDist) 
{
	float3 dist = p0 - p1;
	return normalize(dist) * g_physics_params.springStiffness * (length(dist) - restDist);
}

[numthreads(10, 10, 1)]
void main_cs(uint3 id : SV_DispatchThreadID) 
{
	uint index = id.y * g_physics_params.particleCount.x + id.x;
	if (index > g_physics_params.particleCount.x * g_physics_params.particleCount.y) 
		return;

	// Pinned?
	if (g_particles_in[index].pinned == 1.0) {
		g_particles_out[index].pos = g_particles_out[index].pos;
		g_particles_out[index].vel = 0.0;
		return;
	}

	// Initial force from gravity
	float3 force = g_physics_params.gravity.xyz * g_physics_params.particleMass;

	float3 pos = g_particles_in[index].pos.xyz;
	float3 vel = g_particles_in[index].vel.xyz;

	// Spring forces from neighboring particles
	// left
	if (id.x > 0) {
		force += springForce(g_particles_in[index-1].pos.xyz, pos, g_physics_params.restDistH);
	} 
	// right
	if (id.x < g_physics_params.particleCount.x - 1) {
		force += springForce(g_particles_in[index + 1].pos.xyz, pos, g_physics_params.restDistH);
	}
	// upper
	if (id.y < g_physics_params.particleCount.y - 1) {
		force += springForce(g_particles_in[index + g_physics_params.particleCount.x].pos.xyz, pos, g_physics_params.restDistV);
	} 
	// lower
	if (id.y > 0) {
		force += springForce(g_particles_in[index - g_physics_params.particleCount.x].pos.xyz, pos, g_physics_params.restDistV);
	} 
	// upper-left
	if ((id.x > 0) && (id.y < g_physics_params.particleCount.y - 1)) {
		force += springForce(g_particles_in[index + g_physics_params.particleCount.x - 1].pos.xyz, pos, g_physics_params.restDistD);
	}
	// lower-left
	if ((id.x > 0) && (id.y > 0)) {
		force += springForce(g_particles_in[index - g_physics_params.particleCount.x - 1].pos.xyz, pos, g_physics_params.restDistD);
	}
	// upper-right
	if ((id.x < g_physics_params.particleCount.x - 1) && (id.y < g_physics_params.particleCount.y - 1)) {
		force += springForce(g_particles_in[index + g_physics_params.particleCount.x + 1].pos.xyz, pos, g_physics_params.restDistD);
	}
	// lower-right
	if ((id.x < g_physics_params.particleCount.x - 1) && (id.y > 0)) {
		force += springForce(g_particles_in[index - g_physics_params.particleCount.x + 1].pos.xyz, pos, g_physics_params.restDistD);
	}

	force += (-g_physics_params.damping * vel);

	// Integrate
	float3 f = force * (1.0 / g_physics_params.particleMass);
	g_particles_out[index].pos = float4(pos + vel * g_physics_params.deltaT + 0.5 * f * g_physics_params.deltaT * g_physics_params.deltaT, 1.0);
	g_particles_out[index].vel = float4(vel + f * g_physics_params.deltaT, 0.0);

	// Sphere collision
	float3 sphereDist = g_particles_out[index].pos.xyz - g_physics_params.spherePos.xyz;
	if (length(sphereDist) < g_physics_params.sphereRadius + 0.01) {
		// If the particle is inside the sphere, push it to the outer radius
		g_particles_out[index].pos.xyz = g_physics_params.spherePos.xyz + normalize(sphereDist) * (g_physics_params.sphereRadius + 0.01);		
		// Cancel out velocity
		g_particles_out[index].vel = 0.0;
	}

	// Normals
	if (g_root_consts.calculate_normals == 1) {
		float3 normal = 0.0;
		float3 a, b, c;
		if (id.y > 0) {
			if (id.x > 0) {
				a = g_particles_in[index - 1].pos.xyz - pos;
				b = g_particles_in[index - g_physics_params.particleCount.x - 1].pos.xyz - pos;
				c = g_particles_in[index - g_physics_params.particleCount.x].pos.xyz - pos;
				normal += cross(a,b) + cross(b,c);
			}
			if (id.x < g_physics_params.particleCount.x - 1) {
				a = g_particles_in[index - g_physics_params.particleCount.x].pos.xyz - pos;
				b = g_particles_in[index - g_physics_params.particleCount.x + 1].pos.xyz - pos;
				c = g_particles_in[index + 1].pos.xyz - pos;
				normal += cross(a,b) + cross(b,c);
			}
		}
		if (id.y < g_physics_params.particleCount.y - 1) {
			if (id.x > 0) {
				a = g_particles_in[index + g_physics_params.particleCount.x].pos.xyz - pos;
				b = g_particles_in[index + g_physics_params.particleCount.x - 1].pos.xyz - pos;
				c = g_particles_in[index - 1].pos.xyz - pos;
				normal += cross(a,b) + cross(b,c);
			}
			if (id.x < g_physics_params.particleCount.x - 1) {
				a = g_particles_in[index + 1].pos.xyz - pos;
				b = g_particles_in[index + g_physics_params.particleCount.x + 1].pos.xyz - pos;
				c = g_particles_in[index + g_physics_params.particleCount.x].pos.xyz - pos;
				normal += cross(a,b) + cross(b,c);
			}
		}
		g_particles_out[index].normal = float4(normalize(normal), 0.0f);
	}
}