#ifdef PHI_BACKEND_D3D12
#include "sample.hh"

#include <cmath>


#include <rich-log/log.hh>

#include <clean-core/array.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/commands.hh>
#include <phantasm-hardware-interface/window_handle.hh>

#include <phantasm-hardware-interface/d3d12/BackendD3D12.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>
#include <arcana-incubator/device-abstraction/timer.hh>
#include <arcana-incubator/imgui/imgui.hh>
#include <arcana-incubator/imgui/imgui_impl_phi.hh>
#include <arcana-incubator/imgui/imgui_impl_sdl2.hh>
#include <arcana-incubator/phi-util/texture_creation.hh>

#include "sample_util.hh"
#include "scene.hh"

void phi_test::run_bindless_sample(phi::d3d12::BackendD3D12& backend, sample_config const& sample_config, phi::backend_config const& backend_config)
{
    if (!phi_test::run_onboarding_test())
        return;

    using namespace phi;

    // backend init
    backend.initialize(backend_config);

    // window init
    inc::da::initialize();
    inc::da::SDLWindow window;
    window.initialize(sample_config.window_title);

    // main swapchain creation
    phi::handle::swapchain const main_swapchain = backend.createSwapchain({window.getSdlWindow()}, window.getSize());
    unsigned const msc_num_backbuffers = backend.getNumBackbuffers(main_swapchain);
    phi::format const msc_backbuf_format = backend.getBackbufferFormat(main_swapchain);

    initialize_imgui(window, backend);


    // create PSO
    handle::pipeline_state pso_fullscreen;
    {
        auto const vertex_binary = get_shader_binary("fullscreen_vs", sample_config.shader_ending);
        auto const pixel_binary = get_shader_binary("bindless_fullscreen", sample_config.shader_ending);
        CC_RUNTIME_ASSERT(vertex_binary.is_valid() && pixel_binary.is_valid() && "failed to load shaders");

        arg::graphics_shader shader_stages[2];
        shader_stages[0] = arg::graphics_shader{{vertex_binary.get(), vertex_binary.size()}, shader_stage::vertex};
        shader_stages[1] = arg::graphics_shader{{pixel_binary.get(), pixel_binary.size()}, shader_stage::pixel};

        arg::framebuffer_config fbconf;
        fbconf.add_render_target(msc_backbuf_format);

        pipeline_config config;
        config.cull = cull_mode::front;

        arg::shader_arg_shape shapes[1];
        shapes[0] = arg::shader_arg_shape{3, 0, 1, false};

        pso_fullscreen = backend.createPipelineState(arg::vertex_format{{}, 0}, fbconf, shapes, false, shader_stages, config);
    }

    // load 3 textures
    handle::resource res_textures[3];
    {
        inc::texture_creator tex_creator;
        tex_creator.initialize(backend, "res/phi/shader/bin");
        res_textures[0] = tex_creator.load_texture(phi_test::sample_albedo_path, format::rgba8un, true, true);
        res_textures[1] = tex_creator.load_texture(phi_test::sample_normal_path, format::rgba8un, true, false);
        res_textures[2] = tex_creator.load_texture(phi_test::sample_arm_path, format::rgba8un, true, false);
        tex_creator.free(backend);
    }

    // create shader_view
    handle::shader_view sv_bindless;
    {
        sv_bindless = backend.createEmptyShaderView(3, 1, false);

        // write sampler
        sampler_config sampler;
        sampler.init_default(phi::sampler_filter::min_mag_mip_linear);
        backend.writeShaderViewSamplers(sv_bindless, 0, cc::span{sampler});

        resource_view srvs[3];
        for (auto i = 0u; i < CC_COUNTOF(srvs); ++i)
        {
            srvs[i].init_as_tex2d(res_textures[i], format::rgba8un);
        }
        backend.writeShaderViewSRVs(sv_bindless, 0, srvs);
    }

    auto const on_resize_func = [&]() {
        //
    };

    auto cmd_buf_mem = cc::array<std::byte>(10 * 1024u);

    inc::da::Timer timer;
    int targetDescriptorSlot = 0;
    int targetTextureIndex = 0;
    while (!window.isRequestingClose())
    {
        window.pollEvents();

        if (window.clearPendingResize())
        {
            if (!window.isMinimized())
                backend.onResize(main_swapchain, window.getSize());
        }

        if (window.isMinimized())
        {
            continue;
        }

        auto const frametime = timer.elapsedSeconds();
        timer.restart();


        inc::imgui_new_frame(window.getSdlWindow());

        ImGui::SetNextWindowSize(ImVec2{550, 250}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Bindless Sample"))
        {
            ImGui::Text("Frametime: %.3f ms", frametime * 1000.);
            ImGui::Separator();
            ImGui::Text("You're looking at a fullscreen shader which dynamically indexes");
            ImGui::Text("into an unbounded descriptor array - choosing a texture to sample");
            ImGui::Text("Shader: res/phi/shader/src/bindless/fullscreen_texture_multiplex.hlsl");
            ImGui::Separator();
            ImGui::SliderInt("Descriptor Slot", &targetDescriptorSlot, 0, CC_COUNTOF(res_textures) - 1);
            ImGui::SliderInt("Texture Index", &targetTextureIndex, 0, CC_COUNTOF(res_textures) - 1);
            if (ImGui::Button("Rewrite Descriptor"))
            {
                resource_view srv;
                srv.init_as_tex2d(res_textures[targetTextureIndex], format::rgba8un);
                backend.writeShaderViewSRVs(sv_bindless, targetDescriptorSlot, cc::span{srv});
            }
        }
        ImGui::End();

        if (backend.clearPendingResize(main_swapchain))
            on_resize_func();

        cc::capped_vector<handle::command_list, 3> cmdlists;

        // render / present
        {
            command_stream_writer cmd_writer(cmd_buf_mem.data(), cmd_buf_mem.size());

            auto const ng_backbuffer = backend.acquireBackbuffer(main_swapchain);

            if (!ng_backbuffer.is_valid())
            {
                // acquiring failed, we have to discard the current frame
                continue;
            }

            {
                cmd::transition_resources cmd_trans;
                cmd_trans.add(ng_backbuffer, resource_state::render_target);
                cmd_writer.add_command(cmd_trans);
            }

            {
                cmd::begin_render_pass cmd_brp;
                cmd_brp.viewport = backend.getBackbufferSize(main_swapchain);
                cmd_brp.add_backbuffer_rt(ng_backbuffer);
                cmd_brp.set_null_depth_stencil();
                cmd_writer.add_command(cmd_brp);
            }

            {
                cmd::draw cmd_draw;
                cmd_draw.init(pso_fullscreen, 3);
                cmd_draw.add_shader_arg(phi::handle::null_resource, 0, sv_bindless);
                cmd_writer.add_command(cmd_draw);
            }

            ImGui::Render();
            auto* const drawdata = ImGui::GetDrawData();
            auto const commandsize = ImGui_ImplPHI_GetDrawDataCommandSize(drawdata);
            ImGui_ImplPHI_RenderDrawData(drawdata, {cmd_writer.buffer_head(), commandsize});
            cmd_writer.advance_cursor(commandsize);

            {
                cmd::end_render_pass cmd_erp;
                cmd_writer.add_command(cmd_erp);
            }

            {
                cmd::transition_resources tcmd;
                tcmd.add(ng_backbuffer, resource_state::present);
                cmd_writer.add_command(tcmd);
            }

            cmdlists.push_back(backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size()));
        }

        backend.submit(cmdlists);
        backend.present(main_swapchain);
    }

    backend.flushGPU();

    backend.freeRange(res_textures);
    backend.free(pso_fullscreen);
    backend.free(sv_bindless);
    backend.free(main_swapchain);

    inc::imgui_shutdown();
    backend.destroy();
}

#endif