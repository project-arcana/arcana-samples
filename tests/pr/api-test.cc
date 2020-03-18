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

    pr::Context ctx(phi::window_handle{window.getSdlWindow()});

    ctx.start_capture();

    auto const shader_vertex = ctx.make_shader(sample_shader_text, "main_vs", phi::shader_stage::vertex);
    auto const shader_pixel = ctx.make_shader(sample_shader_text, "main_ps", phi::shader_stage::pixel);

    phi::arg::framebuffer_config fbconf;
    fbconf.add_render_target(pr::format::rgba16f);
    fbconf.add_depth_target(pr::format::depth32f);

    auto const arg_shape = phi::arg::shader_arg_shape{1, 0, 0, true};

    auto const pso = ctx.make_graphics_pipeline_state<inc::assets::simple_vertex>(fbconf, cc::span{arg_shape}, true, {}, shader_vertex, shader_pixel);

    pr::buffer b_indices;
    pr::buffer b_vertices;

    pr::render_target t_depth;
    pr::render_target t_color;

    {
        // load a mesh from disk
        auto const mesh = inc::assets::load_binary_mesh("res/pr/liveness_sample/mesh/ball.mesh");

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

    cc::vector<tg::mat4> modelmats;
    modelmats.emplace_back();

    auto modelmat_srv = ctx.make_upload_buffer(sizeof(tg::mat4) * modelmats.size(), sizeof(tg::mat4));
    auto camconst_cbv = ctx.make_upload_buffer(sizeof(cam_constants));

    ctx.write_buffer(modelmat_srv, modelmats.data(), modelmats.size() * sizeof(modelmats[0]));
    ctx.write_buffer_t(modelmat_srv, cam_constants{tg::mat4::identity});

    pr::shader_view sv;
    sv.add_srv(modelmat_srv);

    auto const svb = ctx.make_argument(sv);

    auto create_targets = [&](tg::isize2 size) {
        t_depth = ctx.make_target(size, pr::format::depth32f);
        t_color = ctx.make_target(size, pr::format::rgba16f);
    };

    create_targets(ctx.get_backbuffer_size());

    while ((void)window.pollEvents(), !window.isRequestingClose())
    {
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
                    auto pass = fb.pipeline(pso);

                    pass.add_argument(svb, camconst_cbv);

                    for (auto i = 0u; i < modelmats.size(); ++i)
                    {
                        pass.write_root_constants(i);
                        pass.draw_indexed(b_vertices, b_indices);
                    }
                }

                ctx.submit(frame);
            }
            ctx.present();
        }
    }

    ctx.flush();
    ctx.end_capture();
}
