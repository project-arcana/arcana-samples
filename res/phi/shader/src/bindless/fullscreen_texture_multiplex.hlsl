
Texture2D gBindlessTextures[]       : register(t0, space0);

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

    float3 color = gBindlessTextures[NonUniformResourceIndex(texture_index)].Sample(gSampler, Texcoord).rgb;
    return float4(color, 1);
}
