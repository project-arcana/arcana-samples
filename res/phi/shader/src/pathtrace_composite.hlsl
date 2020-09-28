
#include "common/tonemappers.hlsli"
#include "common/color_util.hlsli"
#include "common/math.hlsli"

Texture2D<float4> gCurrentIrradiance                                    : register(t0);
Texture2D<float4> gCumulativeIrradiance                                 : register(t1);

struct PathtraceCompositeData
{
    uint num_cumulative_samples;    // amount of samples contained in gCumulativeIrradiance
    uint num_input_samples;         // amount of samples contained in gCurrentIrradiance
    uint2 viewport_size;
};

[[vk::push_constant]] ConstantBuffer<PathtraceCompositeData> gData      : register(b1, space0);


float4 main_ps(
    in noperspective float2 Texcoord : TEXCOORD0
) : SV_TARGET
{
    const int3 pixel = int3(Texcoord * gData.viewport_size, 0);

    const float4 irradiance = gCurrentIrradiance.Load(pixel);
    const float4 cumulative = gCumulativeIrradiance.Load(pixel);
    const float variance = prev_cumulative.a;

    const uint4 new_num_samples = gData.num_cumulative_samples + gData.num_input_samples;

    float3 new_cumulative_rgb = 
        (cumulative.rgb * gData.num_cumulative_samples + irradiance.rgb) 
        / new_num_samples;

    float new_variance = 0.f;

    if (new_num_samples > 1 && new_num_samples < 4096)
    {
        float irr_y = get_luminance(cumulative.rgb);
        float deviation2 = pow2(irr_y - get_luminance(new_cumulative_rgb));

        new_variance = deviation2;

        if (gData.num_cumulative_samples > 0)
        {
            new_variance += variance * (gData.num_cumulative_samples - 1);
        }
        new_variance /= new_num_samples - 1;
    }

    return float4(new_cumulative_rgb, new_variance);
}
