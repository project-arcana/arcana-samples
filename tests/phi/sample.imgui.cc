#include "sample.hh"

#include <cmath>

#include <rich-log/log.hh>

#include <clean-core/capped_array.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-hardware-interface/commands.hh>
#include <phantasm-hardware-interface/detail/unique_buffer.hh>
#include <phantasm-hardware-interface/window_handle.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>
#include <arcana-incubator/device-abstraction/timer.hh>
#include <arcana-incubator/imgui/imgui_impl_pr.hh>
#include <arcana-incubator/imgui/imgui_impl_sdl2.hh>

#include "sample_util.hh"

void phi_test::run_imgui_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& backend_config)
{
    using namespace phi;

    inc::da::SDLWindow window;
    window.initialize(sample_config.window_title);
    backend.initialize(backend_config, window_handle{window.getSdlWindow()});

    // Imgui init
    inc::ImGuiPhantasmImpl imgui_implementation;
    {
        ImGui::SetCurrentContext(ImGui::CreateContext(nullptr));
        ImGui_ImplSDL2_Init(window.getSdlWindow());
        window.setEventCallback(ImGui_ImplSDL2_ProcessEvent);

        {
            auto const ps_bin = get_shader_binary("imgui_ps", sample_config.shader_ending);
            auto const vs_bin = get_shader_binary("imgui_vs", sample_config.shader_ending);
            imgui_implementation.initialize(&backend, ps_bin.get(), ps_bin.size(), vs_bin.get(), vs_bin.size());
        }
    }

    handle::pipeline_state pso_clear;

    {
        auto const vertex_binary = get_shader_binary("fullscreen_vs", sample_config.shader_ending);
        auto const pixel_binary = get_shader_binary("clear_ps", sample_config.shader_ending);

        CC_RUNTIME_ASSERT(vertex_binary.is_valid() && pixel_binary.is_valid() && "failed to load shaders");

        cc::capped_vector<arg::graphics_shader, 6> shader_stages;
        shader_stages.push_back(arg::graphics_shader{{vertex_binary.get(), vertex_binary.size()}, shader_stage::vertex});
        shader_stages.push_back(arg::graphics_shader{{pixel_binary.get(), pixel_binary.size()}, shader_stage::pixel});

        arg::framebuffer_config fbconf;
        fbconf.add_render_target(backend.getBackbufferFormat());

        pipeline_config config;
        config.cull = cull_mode::front;

        pso_clear = backend.createPipelineState(arg::vertex_format{{}, 0}, fbconf, {}, false, shader_stages, config);
    }


    inc::da::Timer timer;
    float run_time = 0.f;
    float log_time = 0.f;

    auto const on_resize_func = [&]() {
        //
    };

    constexpr size_t lc_thread_buffer_size = static_cast<size_t>(10 * 1024u);
    cc::array<std::byte*, 1> thread_cmd_buffer_mem;

    for (auto& mem : thread_cmd_buffer_mem)
        mem = static_cast<std::byte*>(std::malloc(lc_thread_buffer_size));

    CC_DEFER
    {
        for (std::byte* mem : thread_cmd_buffer_mem)
            std::free(mem);
    };

    while (!window.isRequestingClose())
    {
        window.pollEvents();

        if (window.clearPendingResize())
        {
            if (!window.isMinimized())
                backend.onResize({window.getWidth(), window.getHeight()});
        }

        if (!window.isMinimized())
        {
            auto const frametime = timer.elapsedMilliseconds();
            timer.restart();
            run_time += frametime / 1000.f;
            log_time += frametime;

            if (log_time >= 1750.f)
            {
                log_time = 0.f;
                LOG(info)("frametime: %fms", static_cast<double>(frametime));
            }


            if (backend.clearPendingResize())
                on_resize_func();

            cc::capped_vector<handle::command_list, 3> cmdlists;

            // render / present
            {
                command_stream_writer cmd_writer(thread_cmd_buffer_mem[0], lc_thread_buffer_size);

                auto const ng_backbuffer = backend.acquireBackbuffer();

                if (!ng_backbuffer.is_valid())
                {
                    // acquiring failed, and we have to discard the current frame
                    continue;
                }

                {
                    cmd::transition_resources cmd_trans;
                    cmd_trans.add(ng_backbuffer, resource_state::render_target);
                    cmd_writer.add_command(cmd_trans);
                }

                {
                    cmd::begin_render_pass cmd_brp;
                    cmd_brp.viewport = backend.getBackbufferSize();
                    cmd_brp.add_backbuffer_rt(ng_backbuffer);
                    cmd_brp.set_null_depth_stencil();
                    cmd_writer.add_command(cmd_brp);
                }

                {
                    cmd::draw cmd_draw;
                    cmd_draw.init(pso_clear, 3);
                    cmd_writer.add_command(cmd_draw);
                }

                {
                    cmd::end_render_pass cmd_erp;
                    cmd_writer.add_command(cmd_erp);
                }


                ImGui_ImplSDL2_NewFrame(window.getSdlWindow());
                ImGui::NewFrame();
                ImGui::ShowDemoWindow(nullptr);
                ImGui::Render();

                auto* const drawdata = ImGui::GetDrawData();
                auto const commandsize = imgui_implementation.get_command_size(drawdata);
                imgui_implementation.write_commands(ImGui::GetDrawData(), ng_backbuffer, cmd_writer.buffer_head(), commandsize);
                cmd_writer.advance_cursor(commandsize);

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(ng_backbuffer, resource_state::present);
                    cmd_writer.add_command(tcmd);
                }

                cmdlists.push_back(backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size()));
            }

            // submit
            backend.submit(cmdlists);

            // present
            backend.present();
        }
    }

    backend.flushGPU();
    backend.free(pso_clear);

    imgui_implementation.destroy();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}
