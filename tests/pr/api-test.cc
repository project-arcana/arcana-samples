#include <nexus/app.hh>

#include <arcana-incubator/asset-loading/mesh_loader.hh>
#include <arcana-incubator/device-abstraction/device_abstraction.hh>

#include <rich-log/log.hh>

#include <phantasm-renderer/Context.hh>
#include <phantasm-renderer/Frame.hh>
#include <phantasm-renderer/GraphicsPass.hh>
#include <phantasm-renderer/reflection/vertex_attributes.hh>

namespace
{
char const* sample_shader_text = R"(
struct vs_in
{
    float3 P : POSITION;
    float3 N : NORMAL;
    float2 Texcoord : TEXCOORD;
    float4 Tangent : TANGENT;
};

struct vs_out
{
    float4 SV_P : SV_POSITION;
    float3 N : VO_NORM;
};

struct camera_constants
{
    float4x4 view_proj;
};

struct model_constants
{
    uint model_mat_index;
};

StructuredBuffer<float4x4> g_model_matrices         : register(t0, space0);

ConstantBuffer<camera_constants> g_frame_data       : register(b0, space0);

[[vk::push_constant]] ConstantBuffer<model_constants> g_model_data        : register(b1, space0);

vs_out main_vs(vs_in v_in)
{
    vs_out Out;

    const float4x4 model = g_model_matrices[g_model_data.model_mat_index];
    const float4x4 mvp = mul(g_frame_data.view_proj, model);
    Out.SV_P = mul(mvp, float4(v_in.P, 1.0));
    float3 N = normalize(mul((float3x3)model, v_in.N));
    Out.N = normalize(N);
    return Out;
}

float4 main_ps(vs_out p_in) : SV_TARGET
{
    float3 Li =  normalize(float3(-2, 2, 3));
    float dotNL = saturate(dot(p_in.N, Li)) * 0.55;
    return float4(dotNL, dotNL, dotNL, 1.0);
}
)";

char const* blit_shader_text = R"(
struct vs_in
{
   uint vid            : SV_VertexID;
};

struct vs_out
{
   float4 SV_P         : SV_POSITION;
   float2 Texcoord     : TEXCOORD;
};

#define TONEMAP_GAMMA 2.224

Texture2D g_texture                             : register(t0, space0);
SamplerState g_sampler                          : register(s0, space0);

vs_out main_vs(vs_in In)
{
   vs_out Out;
   Out.Texcoord = float2((In.vid << 1) & 2, In.vid & 2);
   Out.SV_P = float4(Out.Texcoord * 2.0f + -1.0f, 0.0f, 1.0f);
   Out.SV_P.y = -Out.SV_P.y;
   return Out;
}

// Uncharted 2 Tonemapper
float3 tonemap_uncharted2(in float3 x)
{
   float A = 0.15;
   float B = 0.50;
   float C = 0.10;
   float D = 0.20;
   float E = 0.02;
   float F = 0.30;

   return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float3 tonemap_uc2(in float3 color)
{
   float W = 11.2;

   color *= 16;  // Hardcoded Exposure Adjustment

   float exposure_bias = 2.0f;
   float3 curr = tonemap_uncharted2(exposure_bias*color);

   float3 white_scale = 1.0f/tonemap_uncharted2(W);
   float3 ccolor = curr*white_scale;

   float3 ret = pow(abs(ccolor), TONEMAP_GAMMA); // gamma

   return ret;
}

float4 main_ps(vs_out In) : SV_TARGET
{
   float4 hdr = g_texture.Sample(g_sampler, In.Texcoord);

   float4 color = float4(tonemap_uc2(hdr.xyz), 1.0);
   return color;
}
)";

struct cam_constants
{
    tg::mat4 vp;
};

}

APP("api_test")
{
    auto ctx = pr::Context(pr::backend::vulkan);

    inc::da::SDLWindow window;
    window.initialize("api test");
    auto swapchain = ctx.make_swapchain(phi::window_handle{window.getSdlWindow()}, window.getSize());

    // pr::graphics_pipeline_state pso_render;
    pr::auto_graphics_pipeline_state pso_blit;

    pr::auto_buffer b_indices;
    pr::auto_buffer b_vertices;

    pr::auto_buffer b_modelmats;
    pr::auto_buffer b_camconsts;

    // pr::auto_render_target t_depth;
    // pr::auto_render_target t_color;

    pr::auto_prebuilt_argument sv_render;

    unsigned const num_instances = 3;

    auto const render_vs = ctx.make_shader(sample_shader_text, "main_vs", phi::shader_stage::vertex);
    auto const pixel_vs = ctx.make_shader(sample_shader_text, "main_ps", phi::shader_stage::pixel);

    // create persisted PSO
    {
        auto const s_vertex = ctx.make_shader(blit_shader_text, "main_vs", phi::shader_stage::vertex);
        auto const s_pixel = ctx.make_shader(blit_shader_text, "main_ps", phi::shader_stage::pixel);

        phi::pipeline_config config;
        config.depth = phi::depth_function::none;
        config.cull = phi::cull_mode::none;

        pso_blit = ctx.make_pipeline_state(pr::graphics_pass(s_vertex, s_pixel).arg(1, 0, 1).config(config),
                                           pr::framebuffer(ctx.get_backbuffer_format(swapchain)));
    }

    // load mesh buffers
    {
        // load a mesh from disk
        auto const mesh = inc::assets::load_binary_mesh("res/arcana-sample-resources/phi/mesh/ball.mesh");

        // create an upload buffer and memcpy the mesh data to it
        auto const upbuff = ctx.make_upload_buffer(mesh.vertices.size_bytes() + mesh.indices.size_bytes());
        ctx.write_to_buffer(upbuff, mesh.vertices);
        ctx.write_to_buffer(upbuff, mesh.indices, mesh.vertices.size_bytes());

        // create device-memory vertex/index buffers
        b_vertices = ctx.make_buffer(mesh.vertices.size_bytes(), sizeof(inc::assets::simple_vertex));
        b_indices = ctx.make_buffer(mesh.indices.size_bytes(), sizeof(uint32_t));

        {
            auto frame = ctx.make_frame();

            // copy to them
            frame.copy(upbuff, b_vertices);
            frame.copy(upbuff, b_indices, mesh.vertices.size_bytes());

            ctx.submit(cc::move(frame));
        }

        ctx.flush();
    }

    // upload
    {
        cc::vector<tg::mat4> modelmats;
        for (auto i = 0u; i < num_instances; ++i)
            modelmats.push_back(tg::translation(tg::pos3((-1 + int(i)) * 3.f, 0, 0)) * tg::scaling(0.21f, 0.21f, 0.21f));

        b_modelmats = ctx.make_upload_buffer(sizeof(tg::mat4) * modelmats.size(), sizeof(tg::mat4));
        b_camconsts = ctx.make_upload_buffer(sizeof(cam_constants));

        ctx.write_to_buffer(b_modelmats, modelmats);
    }

    sv_render = ctx.build_argument().add(b_modelmats).make_graphics();

    tg::isize2 backbuffer_size;

    auto create_targets = [&](tg::isize2 size) {
        //   t_depth = ctx.make_target(size, pr::format::depth32f);
        //   t_color = ctx.make_target(size, pr::format::rgba16f);
        backbuffer_size = size;

        auto const vp = tg::perspective_directx(60_deg, size.width / float(size.height), 0.1f, 10000.f)
                        * tg::look_at_directx(tg::pos3(5, 5, 5), tg::pos3(0, 0, 0), tg::vec3(0, 1, 0));
        ctx.write_to_buffer(b_camconsts, cam_constants{vp});
    };

    create_targets(ctx.get_backbuffer_size(swapchain));

    while (!window.isRequestingClose())
    {
        window.pollEvents();
        if (window.isMinimized())
            continue;

        if (window.clearPendingResize())
        {
            ctx.on_window_resize(swapchain, window.getSize());
            ctx.clear_resource_caches();
        }

        if (ctx.clear_backbuffer_resize(swapchain))
            create_targets(ctx.get_backbuffer_size(swapchain));

        {
            auto frame = ctx.make_frame();

            auto t_depth = ctx.get_target(backbuffer_size, pr::format::depth32f);
            auto t_color = ctx.get_target(backbuffer_size, pr::format::rgba16f);

            {
                auto fb = frame.make_framebuffer(t_color, t_depth);

                // create a pass using cache access
                phi::pipeline_config config;
                config.depth = phi::depth_function::less;
                config.cull = phi::cull_mode::back;
                auto gp = pr::graphics_pass<inc::assets::simple_vertex>(config, render_vs, pixel_vs).arg(1, 0, 0, true).enable_constants();

                // bind a persisted argument, plus a CBV
                auto pass = fb.make_pass(gp).bind(sv_render, b_camconsts);

                for (auto i = 0u; i < num_instances; ++i)
                {
                    pass.write_constants(i);
                    pass.draw(b_vertices, b_indices);
                }
            }

            auto backbuffer = ctx.acquire_backbuffer(swapchain);

            frame.transition(t_color, phi::resource_state::shader_resource, phi::shader_stage::pixel);

            {
                auto fb = frame.make_framebuffer(backbuffer);
                // create a pass from a persisted PSO
                auto pass = fb.make_pass(pso_blit);

                // bind an argument using cache access
                pr::argument arg;
                arg.add(t_color);
                arg.add_sampler(phi::sampler_filter::min_mag_mip_point);

                pass.bind(arg).draw(3);
            }

            frame.transition(backbuffer, phi::resource_state::present);

            ctx.submit(cc::move(frame));
        }

        ctx.present(swapchain);
    }

    ctx.flush();
}
