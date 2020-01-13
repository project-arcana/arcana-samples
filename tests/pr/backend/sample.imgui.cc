#include "sample.hh"

#include <cmath>

#include <rich-log/log.hh>

#include <clean-core/capped_array.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-renderer/backend/command_stream.hh>
#include <phantasm-renderer/backend/detail/unique_buffer.hh>
#include <phantasm-renderer/primitive_pipeline_config.hh>

#include <arcana-incubator/device-abstraction/timer.hh>
#include <arcana-incubator/device-abstraction/window.hh>
#include <arcana-incubator/imgui/imgui_impl_pr.hh>
#include <arcana-incubator/imgui/imgui_impl_win32.hh>

#include "sample_util.hh"
#include "texture_util.hh"

void pr_test::run_imgui_sample(pr::backend::Backend& backend, sample_config const& sample_config, pr::backend::backend_config const& backend_config)
{
    using namespace pr::backend;

    inc::da::Window window;
    window.initialize(sample_config.window_title);
    backend.initialize(backend_config, native_window_handle{window.getNativeHandleA(), window.getNativeHandleB()});

    // Imgui init
#ifdef CC_OS_WINDOWS
    inc::ImGuiPhantasmImpl imgui_implementation;
    {
        ImGui::SetCurrentContext(ImGui::CreateContext(nullptr));
        ImGui_ImplWin32_Init(window.getNativeHandleA());
        window.setEventCallback(ImGui_ImplWin32_WndProcHandler);

        {
            auto const ps_bin = get_shader_binary("res/pr/liveness_sample/shader/bin/imgui_ps.%s", sample_config.shader_ending);
            auto const vs_bin = get_shader_binary("res/pr/liveness_sample/shader/bin/imgui_vs.%s", sample_config.shader_ending);
            imgui_implementation.init(&backend, backend.getNumBackbuffers(), ps_bin.get(), ps_bin.size(), vs_bin.get(), vs_bin.size(), sample_config.align_mip_rows);
        }
    }
#endif

    handle::pipeline_state pso_clear;

    {
        auto const vertex_binary = get_shader_binary("res/pr/liveness_sample/shader/bin/blit_vertex.%s", sample_config.shader_ending);
        auto const pixel_binary = get_shader_binary("res/pr/liveness_sample/shader/bin/clear_pixel.%s", sample_config.shader_ending);

        CC_RUNTIME_ASSERT(vertex_binary.is_valid() && pixel_binary.is_valid() && "failed to load shaders");

        cc::capped_vector<arg::shader_stage, 6> shader_stages;
        shader_stages.push_back(arg::shader_stage{vertex_binary.get(), vertex_binary.size(), shader_domain::vertex});
        shader_stages.push_back(arg::shader_stage{pixel_binary.get(), pixel_binary.size(), shader_domain::pixel});

        arg::framebuffer_config fbconf;
        fbconf.add_render_target(backend.getBackbufferFormat());

        pr::primitive_pipeline_config config;
        config.cull = pr::cull_mode::front;

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
        if (window.isPendingResize())
        {
            if (!window.isMinimized())
                backend.onResize({window.getWidth(), window.getHeight()});
            window.clearPendingResize();
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


                cmdlists.push_back(backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size()));

#ifdef CC_OS_WINDOWS
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                ImGui::ShowDemoWindow(nullptr);
                ImGui::Render();
                cmdlists.push_back(imgui_implementation.render(ImGui::GetDrawData(), ng_backbuffer, true));
#endif
            }

            // submit
            backend.submit(cmdlists);

            // present
            backend.present();
        }
    }

    backend.flushGPU();
    backend.free(pso_clear);

#ifdef CC_OS_WINDOWS
    imgui_implementation.shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
#endif
}
