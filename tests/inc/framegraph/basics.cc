
// NOTE: the framegraph is mainly being developed in a different project
// this test is generally representative of the API but will inevitably constantly break

#if 0
#include <nexus/test.hh>

#include <phantasm-renderer/Frame.hh>

#include <arcana-incubator/pr-util/framegraph/framegraph.hh>
#include <arcana-incubator/pr-util/framegraph/resource_cache.hh>

#include <arcana-incubator/pr-util/demo-renderer/asset_pack.hh>
#include <arcana-incubator/pr-util/demo-renderer/data.hh>

#include <arcana-incubator/pr-util/quick_app.hh>
#include <arcana-incubator/pr-util/resource_loading.hh>
#include <arcana-incubator/pr-util/texture_processing.hh>

#include <arcana-incubator/asset-loading/mesh_loader.hh>

 namespace
{
 enum E_ResourceGuids : uint64_t
{
    E_RESGUID_MAIN_DEPTH,
    E_RESGUID_VELOCITY,
    E_RESGUID_SCENE,
    E_RESGUID_TAA_CURRENT,
    E_RESGUID_POSTFX_HDR,
    E_RESGUID_TONEMAP_OUT,
    E_RESGUID_UNUSED_1,
};

 struct global_state
{
    inc::pre::texture_processing tex_processing;

    pr::auto_graphics_pipeline_state pso_depthpre;

    pr::auto_graphics_pipeline_state pso_pbr;

    pr::auto_prebuilt_argument sv_ibl;
    pr::auto_texture tex_ibl_spec;
    pr::auto_texture tex_ibl_irr;
    pr::auto_texture tex_ibl_lut;

    pr::auto_graphics_pipeline_state pso_taa;

    pr::auto_graphics_pipeline_state pso_tonemap;
    pr::auto_graphics_pipeline_state pso_blit;

    pr::auto_render_target t_taa_history_a;
    pr::auto_render_target t_taa_history_b;

    void load(pr::Context& ctx)
    {
        // IBL preprocessing
        inc::pre::filtered_specular_result specular_intermediate;
        {
            tex_processing.init(ctx, "res/pr/demo_render/bin/preprocess/");
            auto frame = ctx.make_frame();

            specular_intermediate
                = tex_processing.load_filtered_specular_map_from_file(frame, "res/arcana-sample-resources/phi/texture/ibl/mono_lake.hdr");

            tex_ibl_spec = cc::move(specular_intermediate.filtered_env);
            tex_ibl_irr = tex_processing.create_diffuse_irradiance_map(frame, tex_ibl_spec);
            tex_ibl_lut = tex_processing.create_brdf_lut(frame, 256);

            frame.transition(tex_ibl_spec, pr::state::shader_resource, pr::shader::pixel);
            frame.transition(tex_ibl_irr, pr::state::shader_resource, pr::shader::pixel);
            frame.transition(tex_ibl_lut, pr::state::shader_resource, pr::shader::pixel);

            ctx.submit(cc::move(frame));
        }

        // depthpre
        {
            auto [vs, b1] = inc::pre::load_shader(ctx, "mesh/depth_vs", pr::shader::vertex, "res/pr/demo_render/bin/");
            auto [ps, b2] = inc::pre::load_shader(ctx, "mesh/depth_ps", pr::shader::pixel, "res/pr/demo_render/bin/");

            pr::pipeline_config config;
            config.cull = pr::cull_mode::back;
            config.depth = pr::depth_function::greater;

            auto gp = pr::graphics_pass(vs, ps).arg(1, 0, 0, true).enable_constants().vertex<inc::assets::simple_vertex>().config(config);
            pso_depthpre = ctx.make_pipeline_state(gp, pr::framebuffer(pr::format::rg16f).depth(pr::format::depth32f));
        }

        // forward
        {
            auto [vs, b1] = inc::pre::load_shader(ctx, "mesh/pbr_vs", pr::shader::vertex, "res/pr/demo_render/bin/");
            auto [ps, b2] = inc::pre::load_shader(ctx, "mesh/pbr_ps", pr::shader::pixel, "res/pr/demo_render/bin/");

            pr::pipeline_config config;
            config.cull = pr::cull_mode::back;
            config.depth = pr::depth_function::equal;
            config.depth_readonly = true;

            auto gp = pr::graphics_pass(vs, ps)                  //
                          .arg(1, 0, 0, true)                    // Mesh data
                          .arg(3, 0, 2)                          // IBL data, samplers
                          .arg(3, 0, 0)                          // Material data
                          .enable_constants()                    //
                          .config(config)                        //
                          .vertex<inc::assets::simple_vertex>(); //

            pso_pbr = ctx.make_pipeline_state(gp, pr::framebuffer(pr::format::b10g11r11uf).depth(pr::format::depth32f));

            auto lut_sampler = pr::sampler_config(pr::sampler_filter::min_mag_mip_linear, 1);
            lut_sampler.address_u = pr::sampler_address_mode::clamp;
            lut_sampler.address_v = pr::sampler_address_mode::clamp;

            // the textures have been created from outside
            sv_ibl = ctx.build_argument()
                         .add(tex_ibl_spec)
                         .add(tex_ibl_irr)
                         .add(tex_ibl_lut)
                         .add_sampler(pr::sampler_filter::anisotropic, 16)
                         .add_sampler(lut_sampler)
                         .make_graphics();
        }

        // taa
        {
            auto [vs, b1] = inc::pre::load_shader(ctx, "post/fullscreen_vs", pr::shader::vertex, "res/pr/demo_render/bin/");
            auto [ps, b2] = inc::pre::load_shader(ctx, "post/taa_ps", pr::shader::pixel, "res/pr/demo_render/bin/");

            auto gp = pr::graphics_pass(vs, ps).arg(4, 0, 1, true);
            pso_taa = ctx.make_pipeline_state(gp, pr::framebuffer(pr::format::b10g11r11uf));
        }

        // postprocessing + blit
        {
            auto [vs, b1] = inc::pre::load_shader(ctx, "post/fullscreen_vs", pr::shader::vertex, "res/pr/demo_render/bin/");
            auto [ps, b2] = inc::pre::load_shader(ctx, "post/tonemap_ps", pr::shader::pixel, "res/pr/demo_render/bin/");
            auto [ps_blit, b3] = inc::pre::load_shader(ctx, "post/blit_ps", pr::shader::pixel, "res/pr/demo_render/bin/");

            auto gp = pr::graphics_pass(vs, ps).arg(1, 0, 1);
            pso_tonemap = ctx.make_pipeline_state(gp, pr::framebuffer(pr::format::bgra8un));

            pso_blit = ctx.make_pipeline_state(pr::graphics_pass(vs, ps_blit).arg(1, 0, 1), pr::framebuffer(pr::format::bgra8un));
        }

        ctx.flush();
    }

    void onResize(pr::Context& ctx, tg::isize2 size)
    {
        t_taa_history_a = ctx.make_target(size, pr::format::b10g11r11uf);
        t_taa_history_b = ctx.make_target_clone(t_taa_history_a);

        // clear history targets explicitly
        {
            auto frame = ctx.make_frame();
            (void)frame.make_framebuffer(t_taa_history_a);
            (void)frame.make_framebuffer(t_taa_history_b);
            ctx.submit(cc::move(frame));
        }
    }
};

 void populate_graph(inc::frag::GraphBuilder& builder, inc::pre::quick_app* app, global_state* state)
{
    struct depthpre_data
    {
        inc::frag::res_handle depth;
        inc::frag::res_handle velocity;
    };

    builder.addPass<depthpre_data>(
        "depth_pre",
        [&](depthpre_data& data, inc::frag::setup_context& ctx) {
            data.depth = ctx.create(E_RESGUID_MAIN_DEPTH,
                                    phi::arg::create_resource_info::render_target(phi::format::depth32f, ctx.target_size(), 1, 1, {0.f, 0}),
                                    phi::resource_state::depth_write);
            data.velocity = ctx.create(E_RESGUID_VELOCITY,
                                       phi::arg::create_resource_info::render_target(phi::format::rg16f, ctx.target_size(), 1, 1, {255, 255, 255,
                                       255}), phi::resource_state::render_target);
        },
        [=](depthpre_data const& data, inc::frag::exec_context& ctx) {
            auto const t_depth = ctx.get_target(data.depth);
            auto const t_raw_velocity = ctx.get_target(data.velocity);

            //            pr::argument arg;
            //            arg.add(scene->current_frame().sb_modeldata);

            //            auto fb = ctx.frame().build_framebuffer().cleared_target(t_raw_velocity, 1.f, 1.f, 1.f, 1.f).cleared_depth(t_depth,
            //            0.f).make(); auto pass = fb.make_pass(state->pso_depthpre).bind(arg, scene->current_frame().cb_camdata);

            //            for (auto i = 0u; i < scene->instances.size(); ++i)
            //            {
            //                auto const& inst = scene->instances[i];

            //                pass.write_constants(i);

            //                auto const& mesh = ap->getMesh(inst.mesh);
            //                pass.draw(mesh.vertex, mesh.index);
            //            }
        });

    struct forward_data
    {
        inc::frag::res_handle depth;
        inc::frag::res_handle scene;
    };

    builder.addPass<forward_data>(
        "main_forward",
        [&](forward_data& data, inc::frag::setup_context& ctx) {
            data.depth = ctx.read_write(E_RESGUID_MAIN_DEPTH);
            data.scene = ctx.create(E_RESGUID_SCENE, phi::arg::create_resource_info::render_target(phi::format::b10g11r11uf, ctx.target_size()));
        },
        [=](forward_data const& data, inc::frag::exec_context& ctx) {
            auto t_depth = ctx.get_target(data.depth);
            auto t_scene = ctx.get_target(data.scene);

            auto fb = ctx.frame()
                          .build_framebuffer()
                          .cleared_target(t_scene) //
                          .loaded_target(t_depth)
                          .make();


            //            pr::argument model_arg;
            //            model_arg.add(scene->current_frame().sb_modeldata);

            //            auto pass = fb.make_pass(state->pso_pbr) //
            //                            .bind(model_arg, scene->current_frame().cb_camdata)
            //                            .bind(state->sv_ibl);

            //            for (auto i = 0u; i < scene->instances.size(); ++i)
            //            {
            //                auto const& inst = scene->instances[i];
            //                pass.write_constants(i);

            //                auto const& mesh = ap->getMesh(inst.mesh);
            //                pass.bind(ap->getMaterial(inst.mat)).draw(mesh.vertex, mesh.index);
            //            }
        });

    struct taa_data
    {
        inc::frag::res_handle depth;
        inc::frag::res_handle scene;
        inc::frag::res_handle vel;
        inc::frag::res_handle curr;
    };

    builder.addPass<taa_data>(
        "taa_resolve",
        [&](taa_data& data, inc::frag::setup_context& ctx) {
            data.depth = ctx.read(E_RESGUID_MAIN_DEPTH, {pr::state::shader_resource, pr::shader::pixel});
            data.scene = ctx.read(E_RESGUID_SCENE, {pr::state::shader_resource, pr::shader::pixel});
            data.vel = ctx.read(E_RESGUID_VELOCITY, {pr::state::shader_resource, pr::shader::pixel});

            // data.curr = ctx.import(E_RESGUID_TAA_CURRENT, scene->is_history_a ? state->t_taa_history_b : state->t_taa_history_a, pr::state::render_target);

            ctx.move(E_RESGUID_TAA_CURRENT, E_RESGUID_POSTFX_HDR);
        },
        [=](taa_data const& data, inc::frag::exec_context& ctx) {
            auto t_depth = ctx.get_target(data.depth);
            auto t_scene = ctx.get_target(data.scene);
            auto t_vel = ctx.get_target(data.vel);

            auto t_curr = ctx.get_target(data.curr);

            // auto const& t_prev = scene->is_history_a ? state->t_taa_history_a : state->t_taa_history_b;

            // ctx.frame().transition(t_prev, pr::state::shader_resource, pr::shader::pixel);

            auto fb = ctx.frame().make_framebuffer(t_curr);

            pr::argument arg;
            arg.add(t_scene);
            // arg.add(t_prev);
            arg.add(t_depth);
            arg.add(t_vel);
            arg.add_sampler(pr::sampler_filter::min_mag_mip_linear, 1, pr::sampler_address_mode::clamp);

            // fb.make_pass(state->pso_taa).bind(arg, scene->current_frame().cb_camdata).draw(3);
        });

    struct tonemap_data
    {
        inc::frag::res_handle hdr_in;
        inc::frag::res_handle hdr_out;
    };

    builder.addPass<tonemap_data>(
        "tonemap",
        [&](tonemap_data& data, inc::frag::setup_context& ctx) {
            data.hdr_in = ctx.read(E_RESGUID_POSTFX_HDR, {pr::state::shader_resource, pr::shader::pixel});
            data.hdr_out = ctx.create(E_RESGUID_TONEMAP_OUT, phi::arg::create_resource_info::render_target(phi::format::bgra8un, ctx.target_size()),
                                      pr::state::render_target);
            ctx.move(E_RESGUID_TONEMAP_OUT, E_RESGUID_POSTFX_HDR);
        },
        [=](tonemap_data const& data, inc::frag::exec_context& ctx) {
            auto t_hdr_in = ctx.get_target(data.hdr_in);
            auto t_hdr_out = ctx.get_target(data.hdr_out);

            auto fb = ctx.frame().make_framebuffer(t_hdr_out);

            pr::argument arg;
            arg.add(t_hdr_in);
            arg.add_sampler(pr::sampler_filter::min_mag_mip_linear, 1, pr::sampler_address_mode::clamp);

            fb.make_pass(state->pso_tonemap).bind(arg).draw(3);
        });

    struct blitout_data
    {
        inc::frag::res_handle hdr_in;
    };

    builder.addPass<blitout_data>(
        "blitout/imgui",
        [&](blitout_data& data, inc::frag::setup_context& ctx) {
            data.hdr_in = ctx.read(E_RESGUID_POSTFX_HDR, {pr::state::shader_resource, pr::shader::pixel});
            ctx.set_root(); // this pass writes to the backbuffer, set as root
        },
        [=](blitout_data const& data, inc::frag::exec_context& ctx) {
            auto const t_hdr = ctx.get_target(data.hdr_in);
            auto bb = ctx.frame().context().acquire_backbuffer(app->main_swapchain);

            {
                auto fb = ctx.frame().make_framebuffer(bb);
                pr::argument arg;
                arg.add(t_hdr);
                arg.add_sampler(pr::sampler_filter::min_mag_mip_point, 1u);
                fb.make_pass(state->pso_blit).bind(arg).draw(3);
            }

            //            app->render_imgui(ctx.frame(), bb);
            ctx.frame().present_after_submit(bb, app->main_swapchain);
        });
}
}

 TEST("framegraph basics", disabled)
{
    inc::pre::quick_app app(pr::backend::vulkan);

    inc::frag::GraphCache fg_cache(&app.context);
    inc::frag::GraphBuilder fg_builder;
    fg_builder.initialize(app.context);

    global_state state;
    state.load(app.context);

    inc::pre::dmr::AssetPack ap;


    app.main_loop([&](float dt) {
        if (app.context.clear_backbuffer_resize(app.main_swapchain))
        {
            auto const size = app.context.get_backbuffer_size(app.main_swapchain);
            fg_cache.freeAll();
            fg_builder.setMainTargetSize(size);
            // app.context.clear_shader_view_cache();
            state.onResize(app.context, size);
        }

        fg_builder.performInfoImgui();
        app.perform_default_imgui(dt);

        // scene.on_next_frame();
        fg_cache.onNewFrame();
        fg_builder.reset();

        // scene.camdata.fill_data(scene.resolution, app.camera.physical.position, app.camera.physical.forward, scene.halton_index);
        // scene.upload_current_frame(app.context);

        populate_graph(fg_builder, &app, &state);
        fg_builder.compile(fg_cache, cc::system_allocator);

        auto frame = app.context.make_frame();
        fg_builder.execute(&frame);
        app.context.submit(cc::move(frame));
    });

    fg_cache.freeAll();
    ap.freeAll();
}
#endif
