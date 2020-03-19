#include <nexus/test.hh>

#include <arcana-incubator/asset-loading/mesh_loader.hh>
#include <arcana-incubator/device-abstraction/device_abstraction.hh>

#include <phantasm-renderer/Context.hh>
#include <phantasm-renderer/Frame.hh>
#include <phantasm-renderer/PrimitivePipeline.hh>
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
    float dotNL = dot(p_in.N, Li);

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

TEST("pr::api")
{
    //

    inc::da::SDLWindow window;
    window.initialize("api test");

    auto ctx = pr::Context(phi::window_handle{window.getSdlWindow()});

    ctx.start_capture();

    pr::graphics_pipeline_state pso_render;
    pr::graphics_pipeline_state pso_blit;

    pr::buffer b_indices;
    pr::buffer b_vertices;

    pr::buffer b_modelmats;
    pr::buffer b_camconsts;

    pr::render_target t_depth;
    pr::render_target t_color;

    pr::baked_shader_view sv_render;
    pr::baked_shader_view sv_blit;

    unsigned const num_instances = 3;

    // create psos
    {
        static_assert(true, "clang format fix");

        {
            auto const s_vertex = ctx.make_shader(sample_shader_text, "main_vs", phi::shader_stage::vertex);
            auto const s_pixel = ctx.make_shader(sample_shader_text, "main_ps", phi::shader_stage::pixel);

            pso_render = ctx.build_pipeline_state()
                             .add_shader(s_vertex)
                             .add_shader(s_pixel)
                             .add_render_target(pr::format::rgba16f)
                             .add_depth_target(pr::format::depth32f)
                             .set_vertex_format<inc::assets::simple_vertex>()
                             .add_argument_shape(1, 0, 0, true)
                             .add_root_constants()
                             .make_graphics();
        }
        {
            auto const s_vertex = ctx.make_shader(blit_shader_text, "main_vs", phi::shader_stage::vertex);
            auto const s_pixel = ctx.make_shader(blit_shader_text, "main_ps", phi::shader_stage::pixel);

            pso_blit = ctx.build_pipeline_state()
                           .add_shader(s_vertex)
                           .add_shader(s_pixel)
                           .add_render_target(ctx.get_backbuffer_format())
                           .add_argument_shape(1, 0, 1, false)
                           .make_graphics();
        }
    }

    // load mesh buffers
    {
        // load a mesh from disk
        auto const mesh = inc::assets::load_binary_mesh("res/arcana-sample-resources/phi/mesh/ball.mesh");

        // create an upload buffer and memcpy the mesh data to it
        auto const upbuff = ctx.make_upload_buffer(mesh.get_vertex_size_bytes() + mesh.get_index_size_bytes());
        ctx.write_buffer(upbuff, mesh.vertices.data(), mesh.get_vertex_size_bytes());
        ctx.write_buffer(upbuff, mesh.indices.data(), mesh.get_index_size_bytes(), mesh.get_vertex_size_bytes());

        // create device-memory vertex/index buffers
        b_vertices = ctx.make_buffer(mesh.get_vertex_size_bytes(), sizeof(inc::assets::simple_vertex));
        b_indices = ctx.make_buffer(mesh.get_index_size_bytes(), sizeof(uint32_t));

        {
            auto frame = ctx.make_frame();

            // copy to them
            frame.copy(upbuff, b_vertices);
            frame.copy(upbuff, b_indices, mesh.get_vertex_size_bytes());

            ctx.submit(frame);
        }

        ctx.flush();
    }

    // upload
    {
        cc::vector<tg::mat4> modelmats;
        for (auto i = 0u; i < num_instances; ++i)
            modelmats.emplace_back();

        b_modelmats = ctx.make_upload_buffer(sizeof(tg::mat4) * modelmats.size(), sizeof(tg::mat4));
        b_camconsts = ctx.make_upload_buffer(sizeof(cam_constants));

        ctx.write_buffer(b_modelmats, modelmats.data(), modelmats.size() * sizeof(modelmats[0]));
        ctx.write_buffer_t(b_modelmats, cam_constants{tg::mat4::identity});
    }

    {
        pr::shader_view sv;
        sv.add_srv(b_modelmats);
        sv_render = ctx.make_argument(sv);
    }

    auto create_targets = [&](tg::isize2 size) {
        t_depth = ctx.make_target(size, pr::format::depth32f);
        t_color = ctx.make_target(size, pr::format::rgba16f);

        pr::shader_view sv;
        sv.add_srv(t_color);
        sv.add_sampler(phi::sampler_filter::min_mag_mip_point);
        sv_blit = ctx.make_argument(sv);
    };

    create_targets(ctx.get_backbuffer_size());

    while (!window.isRequestingClose())
    {
        window.pollEvents();

        if (!window.isMinimized())
        {
            if (window.clearPendingResize())
                ctx.on_window_resize(window.getSize());

            if (ctx.clear_backbuffer_resize())
                create_targets(ctx.get_backbuffer_size());


            {
                auto frame = ctx.make_frame();

                {
                    auto fb = frame.render_to(t_color, t_depth);
                    auto pass = fb.pipeline(pso_render);

                    pass.add_argument(sv_render, b_camconsts);

                    for (auto i = 0u; i < num_instances; ++i)
                    {
                        pass.write_root_constants(i);
                        pass.draw_indexed(b_vertices, b_indices);
                    }
                }

                auto backbuffer = ctx.acquire_backbuffer();

                frame.transition(backbuffer, phi::resource_state::render_target);
                frame.transition(t_color, phi::resource_state::shader_resource, phi::shader_stage::pixel);

                {
                    auto fb = frame.render_to(backbuffer);
                    auto pass = fb.pipeline(pso_blit);

                    pass.add_argument(sv_blit);
                    pass.draw(3);
                }

                frame.transition(backbuffer, phi::resource_state::present);

                ctx.submit(frame);
            }

            ctx.present();
        }
    }

    ctx.flush();
    ctx.end_capture();
}

#if 0
// bind versions
{
    auto pass = ...;

    struct my_data
    {
        int idx;
        tg::color4 color;
    };

    // trivially copyable T
    {
        tg::mat4 transform = ...;
        pass.bind(transform).draw(...);

        my_data data = ...;
        pass.bind(data).draw(...);
    }

    // contiguous range of trivially copyable T
    {
        cc::vector<tg::mat4> transforms = ...;
        pass.bind(transforms).draw(...);

        cc::array<my_data> data = ...;
        pass.bind(data).draw(...);
    }

    // custom setup
    {
        struct my_arg : pr::shader_arg
        {
            tg::mat4 model;
            pr::ImageView2D tex_albedo;
            pr::ImageView2D tex_normal;

            // alternatively also via reflection
            void setup()
            {
                add(model);
                add(tex_albedo);
                add(tex_normal);
            }
        };

        my_arg arg = ...;
        pass.bind(arg).draw(...);
    }

    // directly some resources
    {
        pr::Buffer buffer = ...;
        pass.bind(buffer).draw(...);

        cc::vector<pr::Buffer> buffers = ...;
        pass.bind(buffers).draw(...);

        pr::Image2D tex = ...;
        pass.bind(tex).draw(...);

        cc::vector<pr::Image2D> textures = ...;
        pass.bind(textures).draw(...);

        pr::Image2D texA, texB, texC = ...;
        pass.bind({texA, texB, texC}).draw(...);
    }

    // shader arg builder
    {
        pr::Buffer buffer = ...;
        cc::vector<pr::Buffer> buffers = ...;
        pr::Image2D tex = ...;
        cc::vector<pr::Image2D> textures = ...;
        my_data data = ...;
        tg::mat4 transform = ...;

        pr::shader_arg arg;
        arg.add(buffer);
        arg.add(buffers);
        arg.add(tex, "my_tex"); // name is optional for verification
        arg.add(textures);
        arg.add(data);
        arg.add(transform);
        pass.bind(arg).draw(...);
    }
}
#endif
