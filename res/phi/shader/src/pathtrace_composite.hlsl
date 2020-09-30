
#include "common/tonemappers.hlsli"
#include "common/color_util.hlsli"
#include "common/math.hlsli"
#include "common/random.hlsli"

Texture2D<float4> gCurrentIrradiance                                    : register(t0);
Texture2D<float4> gCumulativeIrradiance                                 : register(t1);

Texture2D<uint> gCurrentNumRays                                         : register(t2);
Texture2D<uint> gCumulativeNumRays                                      : register(t3);

struct PathtraceCompositeData
{
    uint cumulative_samples;    // amount of samples contained in gCumulativeIrradiance
    uint new_samples;         // amount of samples contained in gCurrentIrradiance
    uint2 viewport_size;
};

[[vk::push_constant]] ConstantBuffer<PathtraceCompositeData> gData      : register(b1, space0);


void main_ps(
    in noperspective float2 in_uv : TEXCOORD0,
    out float4 out_tonemapped_color : SV_Target0,
    out float4 out_cumulative_irradiance : SV_Target1,
    out uint out_cumulative_num_rays : SV_Target2
)
{
    const int3 pixel = int3(in_uv * gData.viewport_size, 0);

    // sample irradiance
    const float4 new_irradiance = gCurrentIrradiance.Load(pixel);
    const float4 cumulative = gCumulativeIrradiance.Load(pixel);

    // sample ray counts
    const float new_samples = gCurrentNumRays.Load(pixel);
    float cumulative_samples = gCumulativeNumRays.Load(pixel);

    // reset cumulative count if this is a new frame
    if (gData.cumulative_samples == 0)
        cumulative_samples = 0;

    // sum the amounts
    // max not normally required as new_samples is usually not 0
    out_cumulative_num_rays = max(1, cumulative_samples + new_samples); 

    // acc <- ((n - 1) * acc + of) / n
    // extend the average over all samples of this pixel
    float3 new_cumulative_rgb = ((cumulative.rgb * cumulative_samples) + new_irradiance.rgb) / out_cumulative_num_rays;

    out_cumulative_irradiance = float4(new_cumulative_rgb, 1.f);

#define DEBUG_IGNORE_ACCUMULATION 0

#if DEBUG_IGNORE_ACCUMULATION

    // ignore accumulation in output
    out_tonemapped_color = float4(pow(tonemap_aces_fitted(new_irradiance.rgb / gData.new_samples), 1.0f / 2.224f), 1.f);

#else
    // tonemap
    float3 final_color = tonemap_aces_fitted(new_cumulative_rgb);

    // gamma
    final_color = pow(final_color, 1.0f / 2.224f);

    // dither
    final_color = dither(final_color, pixel.xy, 100);

    out_tonemapped_color = float4(final_color, 1.f);
#endif
}
