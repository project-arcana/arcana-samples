
// classic bindless setup: sufficiently large descriptor arrays for each type of resource
// in SM 6.6 this can be avoided using direct ResourceDescriptorHeap indexing
// note the registers not being t0,t1,t2 but incremented as if all of these descriptors were inline

Texture2D gTextures2D[1024]         : register(t0, space0);

// this is how you would continue with this sort of setup for different types:
// explicit vulkan binding numbers because DXC would otherwise use the same mapping as with SRV registers
// [[vk::binding(1001)]]
// Texture1D gTextures1D[1024]         : register(t1024, space0);

// [[vk::binding(1002)]]
// Texture3D gTextures3D[1024]         : register(t2048, space0);

// [[vk::binding(1003)]]
// TextureCube gTexturesCube[1024]     : register(t3072, space0);

SamplerState gSampler               : register(s0, space0);

float4 main_ps(
        float2 Texcoord : TEXCOORD0,
        float4 SV_P : SV_POSITION
    ) : SV_TARGET
{
    uint texture_index = 0;

    if (Texcoord.x < 0.333f)
    {
        texture_index = 0;
    }
    else if (Texcoord.x < 0.667f)
    {
        texture_index = 1;
    }
    else
    {
        texture_index = 2;
    }

    float3 color = gTextures2D[NonUniformResourceIndex(texture_index)].Sample(gSampler, Texcoord).rgb;
    return float4(color, 1);
}
