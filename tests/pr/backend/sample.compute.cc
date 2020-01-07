#include "sample.hh"

#include <cmath>
#include <iostream>

#include <clean-core/capped_array.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-renderer/backend/command_stream.hh>
#include <phantasm-renderer/backend/detail/unique_buffer.hh>
#include <phantasm-renderer/backend/device_tentative/timer.hh>
#include <phantasm-renderer/backend/device_tentative/window.hh>

#include "sample_util.hh"
#include "texture_util.hh"

namespace
{
constexpr unsigned gc_max_num_backbuffers = 4;
constexpr auto lc_cloth_gridsize = tg::usize2(60, 60);
constexpr unsigned lc_num_cloth_particles = lc_cloth_gridsize.width * lc_cloth_gridsize.height;
}

void pr_test::run_compute_sample(pr::backend::Backend& backend, sample_config const& sample_config, pr::backend::backend_config const& backend_config)
{
    struct particle_t
    {
        tg::vec4 pos = tg::vec4(5, 5, 5, 1);
        tg::vec4 vel = tg::vec4(0, 0, 0, 0);
    };

    using namespace pr::backend;
    CC_RUNTIME_ASSERT(backend_config.num_backbuffers <= gc_max_num_backbuffers && "increase gc_max_num_backbuffers");

    constexpr unsigned lc_cloth_buffer_size = lc_num_cloth_particles * sizeof(particle_t);

    pr::backend::device::Window window;
    window.initialize(sample_config.window_title);
    backend.initialize(backend_config, window);

    struct
    {
        handle::pipeline_state pso_compute;
        handle::shader_view sv_uav;
        handle::resource buf_uav;
    } res;

    // load compute shader
    {
        auto const sb_compute_cloth = get_shader_binary("res/pr/liveness_sample/shader/bin/cloth/compute_simple.%s", sample_config.shader_ending);
        CC_RUNTIME_ASSERT(sb_compute_cloth.is_valid() && "failed to load shaders");

        cc::capped_vector<arg::shader_argument_shape, 1> arg_shape;
        {
            arg::shader_argument_shape shape;
            shape.has_cb = false;
            shape.num_srvs = 0;
            shape.num_uavs = 1;
            shape.num_samplers = 0;
            arg_shape.push_back(shape);
        }

        res.pso_compute = backend.createComputePipelineState(
            arg_shape, arg::shader_stage{sb_compute_cloth.get(), sb_compute_cloth.size(), shader_domain::compute}, false);
    }

    // create resources
    res.buf_uav = backend.createBuffer(lc_cloth_buffer_size, sizeof(particle_t), true);

    // data upload - particles
    {
        cc::vector<particle_t> particles(lc_cloth_gridsize.width * lc_cloth_gridsize.height);

        auto const staging_buf_handle = backend.createMappedBuffer(lc_cloth_buffer_size);
        auto* const staging_buf_map = backend.getMappedMemory(staging_buf_handle);
        std::memcpy(staging_buf_map, particles.data(), lc_cloth_buffer_size);
        backend.flushMappedMemory(staging_buf_handle);

        pr_test::temp_cmdlist copy_cmdlist(&backend, 1024u);

        cmd::copy_buffer ccmd;
        ccmd.init(staging_buf_handle, res.buf_uav, lc_cloth_buffer_size);
        copy_cmdlist.writer.add_command(ccmd);

        copy_cmdlist.finish();

        backend.free(staging_buf_handle);
    }

    {
        shader_view_element sve;
        sve.init_as_structured_buffer(res.buf_uav, lc_num_cloth_particles, sizeof(particle_t));
        res.sv_uav = backend.createShaderView({}, cc::span{sve}, {}, true);
    }

    pr::backend::device::Timer timer;
    float run_time = 0.f;
    unsigned framecounter = 440;

    auto const on_resize_func = [&]() {
        //
    };

    constexpr size_t lc_thread_buffer_size = static_cast<size_t>(10 * 1024u);
    cc::array<std::byte*, 2> thread_cmd_buffer_mem;

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

            ++framecounter;
            if (framecounter == 480)
            {
                std::cout << "Frametime: " << frametime << "ms" << std::endl;
                framecounter = 0;
            }

            if (backend.clearPendingResize())
                on_resize_func();

            // compute
            handle::command_list compute_cmdlist = handle::null_command_list;
            {
                command_stream_writer cmd_writer(thread_cmd_buffer_mem[1], lc_thread_buffer_size);

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(res.buf_uav, resource_state::unordered_access, shader_domain_bits::compute);
                    cmd_writer.add_command(tcmd);
                }

                {
                    cmd::dispatch dcmd;
                    dcmd.init(res.pso_compute, lc_cloth_gridsize.width / 10, lc_cloth_gridsize.height / 10, 1);
                    dcmd.add_shader_arg(handle::null_resource, 0, res.sv_uav);
                    cmd_writer.add_command(dcmd);
                }

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(res.buf_uav, resource_state::vertex_buffer, shader_domain_bits::vertex);
                    cmd_writer.add_command(tcmd);
                }

                compute_cmdlist = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
            }

            // render / present
            handle::command_list present_cmd_list = handle::null_command_list;
            {
                command_stream_writer cmd_writer(thread_cmd_buffer_mem[0], lc_thread_buffer_size);

                auto const ng_backbuffer = backend.acquireBackbuffer();

                if (!ng_backbuffer.is_valid())
                {
                    // The vulkan-only scenario: acquiring failed, and we have to discard the current frame
                    backend.discard(cc::span{compute_cmdlist});
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
                    cmd::end_render_pass cmd_erp;
                    cmd_writer.add_command(cmd_erp);
                }

                {
                    cmd::transition_resources cmd_trans;
                    cmd_trans.add(ng_backbuffer, resource_state::present);
                    cmd_writer.add_command(cmd_trans);
                }
                present_cmd_list = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
            }

            // submit
            cc::array const submission = {compute_cmdlist, present_cmd_list};
            backend.submit(submission);

            // present
            backend.present();
        }
    }


    backend.flushGPU();
    backend.free(res.pso_compute);
    backend.free(res.buf_uav);
    backend.free(res.sv_uav);
}
