//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

static const float softeningSquared    = 0.0012500000f * 0.0012500000f;
static const float g_fG                = 6.67300e-11f * 10000.0f;
static const float g_fParticleMass    = g_fG * 10000.0f * 10000.0f;

static const float g_deltaTime = 0.1f;
static const float g_damping = 1.0f;

#define MAX_PARTICLES 10000
#define BLOCK_SIZE 128
#define DIM_X int(ceil(MAX_PARTICLES / 128.0f))
#define EPILOGUE_PARTICLE_CORRECTION (DIM_X * BLOCK_SIZE - MAX_PARTICLES)

groupshared float4 sharedPos[BLOCK_SIZE];

//
// Body to body interaction, acceleration of the particle at position 
// bi is updated.
//
void bodyBodyInteraction(inout float3 ai, float4 bj, float4 bi, float mass, int particles) 
{
    float3 r = bj.xyz - bi.xyz;

    float distSqr = dot(r, r);
    distSqr += softeningSquared;

    float invDist = 1.0f / sqrt(distSqr);
    float invDistCube =  invDist * invDist * invDist;
    
    float s = mass * invDistCube * particles;

    ai += r * s;
}

struct PosVelo
{
    float4 pos;
    float4 velo;
};

StructuredBuffer<PosVelo> oldPosVelo            : register(t0, space0);    // SRV
RWStructuredBuffer<PosVelo> newPosVelo          : register(u0, space0);    // UAV

[numthreads(BLOCK_SIZE, 1, 1)]
void CSMain(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    // Each thread of the CS updates one of the particles.
    float4 pos = oldPosVelo[DTid.x].pos;
    float4 vel = oldPosVelo[DTid.x].velo;
    float3 accel = 0;
    float mass = g_fParticleMass;

    // Update current particle using all other particles.
    [loop]
    for (uint tile = 0; tile < DIM_X; tile++)
    {
        // Cache a tile of particles unto shared memory to increase IO efficiency.
        sharedPos[GI] = oldPosVelo[tile * BLOCK_SIZE + GI].pos;
       
        GroupMemoryBarrierWithGroupSync();

        [unroll]
        for (uint counter = 0; counter < BLOCK_SIZE; counter += 8 ) 
        {
            bodyBodyInteraction(accel, sharedPos[counter], pos, mass, 1);
            bodyBodyInteraction(accel, sharedPos[counter+1], pos, mass, 1);
            bodyBodyInteraction(accel, sharedPos[counter+2], pos, mass, 1);
            bodyBodyInteraction(accel, sharedPos[counter+3], pos, mass, 1);
            bodyBodyInteraction(accel, sharedPos[counter+4], pos, mass, 1);
            bodyBodyInteraction(accel, sharedPos[counter+5], pos, mass, 1);
            bodyBodyInteraction(accel, sharedPos[counter+6], pos, mass, 1);
            bodyBodyInteraction(accel, sharedPos[counter+7], pos, mass, 1);
        }
        
        GroupMemoryBarrierWithGroupSync();
    }  

    // g_param.x is the number of our particles, however this number might not 
    // be an exact multiple of the tile size. In such cases, out of bound reads 
    // occur in the process above, which means there will be tooManyParticles 
    // "phantom" particles generating false gravity at position (0, 0, 0), so 
    // we have to subtract them here. NOTE, out of bound reads always return 0 in CS.
    const int tooManyParticles = EPILOGUE_PARTICLE_CORRECTION;
    bodyBodyInteraction(accel, float4(0, 0, 0, 0), pos, mass, -tooManyParticles);

    // Update the velocity and position of current particle using the 
    // acceleration computed above.
    vel.xyz += accel.xyz * g_deltaTime;
    vel.xyz *= g_damping;
    pos.xyz += vel.xyz * g_deltaTime;

    if (DTid.x < MAX_PARTICLES)
    {
        newPosVelo[DTid.x].pos = pos;
        newPosVelo[DTid.x].velo = float4(vel.xyz, length(accel));
    }
}
