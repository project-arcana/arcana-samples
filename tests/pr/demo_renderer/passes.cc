#include "passes.hh"

#include <cstdio>

#include <phantasm-hardware-interface/Backend.hh>

#include <arcana-incubator/asset-loading/mesh_loader.hh>
#include <arcana-incubator/pr-util/resource_loading.hh>

#include <phantasm-renderer/pr.hh>

void dmr::depthpre_pass::init(pr::Context& ctx)
{
    auto [vs, b1] = inc::pre::load_shader(ctx, "mesh/depth_vs", pr::shader::vertex, "res/pr/demo_render/bin/");
    auto [ps, b2] = inc::pre::load_shader(ctx, "mesh/depth_ps", pr::shader::pixel, "res/pr/demo_render/bin/");

    pr::pipeline_config config;
    config.cull = pr::cull_mode::back;
    config.depth = pr::depth_function::greater;

    auto gp = pr::graphics_pass(vs, ps).arg(1, 0, 0, true).enable_constants().vertex<inc::assets::simple_vertex>().config(config);
    pso_depthpre = ctx.make_pipeline_state(gp, pr::framebuffer(pr::format::rg16f).depth(pr::format::depth32f));
}

void dmr::depthpre_pass::execute(pr::Context&, pr::raii::Frame& frame, global_targets& targets, dmr::scene& scene)
{
    auto dbg_label = frame.scoped_debug_label("depth pre pass");

    pr::argument arg;
    arg.add(scene.current_frame().sb_modeldata);

    auto fb = frame.build_framebuffer().cleared_target(targets.t_forward_velocity, 1.f, 1.f, 1.f, 1.f).cleared_depth(targets.t_depth, 0.f).make();
    auto pass = fb.make_pass(pso_depthpre).bind(arg, scene.current_frame().cb_camdata);

    for (auto i = 0u; i < scene.instances.size(); ++i)
    {
        auto const& inst = scene.instances[i];
        pass.write_constants(i);
        pass.draw(inst.mesh.vertex, inst.mesh.index);
    }
}

void dmr::forward_pass::init(pr::Context& ctx)
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

void dmr::forward_pass::execute(pr::Context&, pr::raii::Frame& frame, global_targets& targets, dmr::scene& scene)
{
    auto dbg_label = frame.scoped_debug_label("forward pass");

    auto fb = frame.build_framebuffer()
                  .cleared_target(targets.t_forward_hdr) //
                  .loaded_target(targets.t_depth)
                  .make();


    pr::argument model_arg;
    model_arg.add(scene.current_frame().sb_modeldata);

    auto pass = fb.make_pass(pso_pbr) //
                    .bind(model_arg, scene.current_frame().cb_camdata)
                    .bind(sv_ibl);

    for (auto i = 0u; i < scene.instances.size(); ++i)
    {
        auto const& inst = scene.instances[i];
        pass.write_constants(i);
        pass.bind(inst.mat.outer_sv).draw(inst.mesh.vertex, inst.mesh.index);
    }
}

void dmr::taa_pass::init(pr::Context& ctx)
{
    auto [vs, b1] = inc::pre::load_shader(ctx, "post/fullscreen_vs", pr::shader::vertex, "res/pr/demo_render/bin/");
    auto [ps, b2] = inc::pre::load_shader(ctx, "post/taa_ps", pr::shader::pixel, "res/pr/demo_render/bin/");

    auto gp = pr::graphics_pass(vs, ps).arg(4, 0, 1, true);
    pso_taa = ctx.make_pipeline_state(gp, pr::framebuffer(pr::format::b10g11r11uf));
}

void dmr::taa_pass::execute(pr::Context&, pr::raii::Frame& frame, global_targets& targets, dmr::scene& scene)
{
    auto dbg_label = frame.scoped_debug_label("taa resolve");

    auto const& history_target = scene.is_history_a ? targets.t_history_a : targets.t_history_b;
    auto const& history_src = scene.is_history_a ? targets.t_history_b : targets.t_history_a;
    frame.transition(targets.t_forward_hdr, pr::state::shader_resource, pr::shader::pixel);
    frame.transition(targets.t_depth, pr::state::shader_resource, pr::shader::pixel);
    frame.transition(targets.t_forward_velocity, pr::state::shader_resource, pr::shader::pixel);
    frame.transition(history_src, pr::state::shader_resource, pr::shader::pixel);

    auto fb = frame.make_framebuffer(history_target);

    pr::argument arg;
    arg.add(targets.t_forward_hdr);
    arg.add(history_src);
    arg.add(targets.t_depth);
    arg.add(targets.t_forward_velocity);
    arg.add_sampler(pr::sampler_filter::min_mag_mip_linear, 1, pr::sampler_address_mode::clamp);

    fb.make_pass(pso_taa).bind(arg, scene.current_frame().cb_camdata).draw(3);
}

void dmr::postprocess_pass::init(pr::Context& ctx)
{
    auto [vs, b1] = inc::pre::load_shader(ctx, "post/fullscreen_vs", pr::shader::vertex, "res/pr/demo_render/bin/");
    auto [ps, b2] = inc::pre::load_shader(ctx, "post/tonemap_ps", pr::shader::pixel, "res/pr/demo_render/bin/");

    auto gp = pr::graphics_pass(vs, ps).arg(1, 0, 1);
    pso_tonemap = ctx.make_pipeline_state(gp, pr::framebuffer(pr::format::bgra8un));
}

void dmr::postprocess_pass::execute_output(pr::Context&, pr::raii::Frame& frame, dmr::global_targets& targets, dmr::scene& scene, const pr::texture& backbuffer)
{
    auto dbg_label = frame.scoped_debug_label("postprocessing");

    auto const& active_history = scene.is_history_a ? targets.t_history_a : targets.t_history_b;

    frame.transition(active_history, pr::state::shader_resource, pr::shader::pixel);

    {
        auto fb = frame.make_framebuffer(backbuffer);

        pr::argument arg;
        arg.add(active_history);
        arg.add_sampler(pr::sampler_filter::min_mag_mip_linear, 1, pr::sampler_address_mode::clamp);

        fb.make_pass(pso_tonemap).bind(arg).draw(3);
    }
}

void dmr::postprocess_pass::clear_target(pr::raii::Frame& frame, const pr::texture& target)
{
    // simply create (and destroy) a framebuffer, the begin_render_pass will clear the target
    auto fb = frame.make_framebuffer(target);
}
