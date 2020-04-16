#include "common/fullscreen_vs_inout.hlsli"
#include "common/cam_constants.hlsli"
#include "common/util.hlsli"

Texture2D<float4> g_scene_buffer                : register(t0, space0);
Texture2D<float4> g_history_buffer              : register(t1, space0);
Texture2D<float>  g_depth_buffer                : register(t2, space0);
Texture2D<float2> g_velocity_buffer             : register(t3, space0);

SamplerState g_screen_sampler                   : register(s0, space0);

ConstantBuffer<camera_constants> g_frame_data   : register(b0, space0);

float2 queryInverseResolution()
{
    float width, height;
    g_scene_buffer.GetDimensions(width, height);
    return float2(
        1.f / width,
        1.f / height
    );
}

float4 main(vs_out v_in) : SV_TARGET
{
	// 1.6 / (length_of_jitter_sequence + 1)
    const float blendFactor = 1.6f / 9.f;

	//=========================================================
	// Find the location where the history pixel is
	//=========================================================
	float2 velocity = g_velocity_buffer.Sample(g_screen_sampler, v_in.Texcoord);
	float2 previousUV = v_in.Texcoord;
	if (velocity.x >= 1)
	{
		// depth is jittered, use jittered VP for reconstruction
		const float pixel_depth = g_depth_buffer.Sample(g_screen_sampler, v_in.Texcoord);
		const float3 current_worldpos = reconstruct_worldspace(v_in.Texcoord, pixel_depth, g_frame_data.vp_inv);
		// calculate previous HDC position with previous clean VP
		const float4 prev_hdc = mul(g_frame_data.prev_clean_vp, float4(current_worldpos, 1));

		previousUV = convert_hdc_to_uv(prev_hdc);
	}
	else
	{
		// velocity = current_uv - previous_uv
		previousUV -= velocity;
	}

	//=========================================================
	// Sample the current neighbourhood
	//=========================================================
    const float2 invResolution = queryInverseResolution();
    const float texelWidth = invResolution.x;
    const float texelHeight = invResolution.y;
	const float4 nbh[9] = 
	{
		rgba_to_ycocg(g_scene_buffer.Sample(g_screen_sampler, float2(v_in.Texcoord.x - texelWidth, v_in.Texcoord.y - texelHeight))),
		rgba_to_ycocg(g_scene_buffer.Sample(g_screen_sampler, float2(v_in.Texcoord.x - texelWidth, v_in.Texcoord.y              ))),
		rgba_to_ycocg(g_scene_buffer.Sample(g_screen_sampler, float2(v_in.Texcoord.x - texelWidth, v_in.Texcoord.y + texelHeight))),
		rgba_to_ycocg(g_scene_buffer.Sample(g_screen_sampler, float2(v_in.Texcoord.x,              v_in.Texcoord.y - texelHeight))),
		rgba_to_ycocg(g_scene_buffer.Sample(g_screen_sampler, float2(v_in.Texcoord.x,              v_in.Texcoord.y              ))),
		rgba_to_ycocg(g_scene_buffer.Sample(g_screen_sampler, float2(v_in.Texcoord.x,              v_in.Texcoord.y + texelHeight))),
		rgba_to_ycocg(g_scene_buffer.Sample(g_screen_sampler, float2(v_in.Texcoord.x + texelWidth, v_in.Texcoord.y - texelHeight))),
		rgba_to_ycocg(g_scene_buffer.Sample(g_screen_sampler, float2(v_in.Texcoord.x + texelWidth, v_in.Texcoord.y              ))),
		rgba_to_ycocg(g_scene_buffer.Sample(g_screen_sampler, float2(v_in.Texcoord.x + texelWidth, v_in.Texcoord.y + texelHeight))),
	};
	const float4 color = nbh[4];

	//=========================================================
	// Create an YCoCg min/max box to clip history to
	//=========================================================
	const float4 minimum = min(min(min(min(min(min(min(min(nbh[0], nbh[1]), nbh[2]), nbh[3]), nbh[4]), nbh[5]), nbh[6]), nbh[7]), nbh[8]);
	const float4 maximum = max(max(max(max(max(max(max(max(nbh[0], nbh[1]), nbh[2]), nbh[3]), nbh[4]), nbh[5]), nbh[6]), nbh[7]), nbh[8]);
	const float4 average = (nbh[0] + nbh[1] + nbh[2] + nbh[3] + nbh[4] + nbh[5] + nbh[6] + nbh[7] + nbh[8]) * .1111111111111111111111111f;

	//=========================================================
	// History clipping
	//=========================================================
	float4 history = rgba_to_ycocg(g_history_buffer.Sample(g_screen_sampler, previousUV));
	
	const float3 origin = history.rgb - 0.5f*(minimum.rgb + maximum.rgb);
	const float3 direction = average.rgb - history.rgb;
	const float3 extents = maximum.rgb - 0.5f*(minimum.rgb + maximum.rgb);

	history = lerp(history, average, saturate(intersect_aabb(origin, direction, extents)));

	//=========================================================
	// Calculate results
	//=========================================================
	float impulse = abs(color.x - history.x) / max(color.x, max(history.x, minimum.x));
	float factor = lerp(blendFactor * 0.8f, blendFactor * 2.0f, impulse*impulse);

	return ycocg_to_rgba(lerp(history, color, saturate(factor)));
}