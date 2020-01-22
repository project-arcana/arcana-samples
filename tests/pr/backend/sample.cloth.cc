#include "sample.hh"

#include <cmath>
#include <iostream>

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

#include "sample_util.hh"
#include "texture_util.hh"

namespace
{
constexpr unsigned gc_max_num_backbuffers = 4;
constexpr auto lc_cloth_gridsize = tg::usize2(60, 60);
constexpr auto lc_cloth_worldsize = tg::fsize2(2.5f, 2.5f);
constexpr unsigned lc_num_cloth_particles = lc_cloth_gridsize.width * lc_cloth_gridsize.height;
}

void pr_test::run_cloth_sample(phi::Backend& backend, const pr_test::sample_config& sample_config, const phi::backend_config& backend_config)
{
    struct particle_t
    {
        tg::vec4 pos;
        tg::vec4 vel;
        tg::vec4 uv;
        tg::vec4 normal;
        float pinned;
    };

    struct physics_constants_t
    {
        float deltaT = 0.f;
        float particleMass = 0.1f;
        float springStiffness = 2000.f;
        float damping = 0.25f;
        float restDistH = 0.f;
        float restDistV = 0.f;
        float restDistD = 0.f;
        float sphereRadius = 0.5f;
        tg::vec4 spherePos = tg::vec4(0.f);
        tg::vec4 gravity = tg::vec4(0.f, 9.8f, 0.f, 0.f);
        tg::ivec2 particleCount = tg::ivec2(0);

        void init()
        {
            float const dx = lc_cloth_worldsize.width / (lc_cloth_worldsize.width - 1);
            float const dy = lc_cloth_worldsize.height / (lc_cloth_worldsize.height - 1);

            restDistH = dx;
            restDistV = dy;
            restDistD = std::sqrt(dx * dx + dy * dy);
            particleCount.x = lc_cloth_gridsize.width;
            particleCount.y = lc_cloth_gridsize.height;
        }

        void update(float dt)
        {
            deltaT = dt;
            if (deltaT > 0.f)
            {
                // TODO
                gravity = tg::vec4(0.f);
            }
        }
    };

    using namespace phi;
    CC_RUNTIME_ASSERT(backend_config.num_backbuffers <= gc_max_num_backbuffers && "increase gc_max_num_backbuffers");

    constexpr unsigned lc_cloth_buffer_size = lc_num_cloth_particles * sizeof(particle_t);

    inc::da::SDLWindow window;
    window.initialize(sample_config.window_title);
    backend.initialize(backend_config, {window.getSdlWindow()});

    struct
    {
        handle::pipeline_state pso_compute_cloth;
        handle::shader_view sv_compute_cloth_a;
        handle::shader_view sv_compute_cloth_b;

        handle::resource buf_input;
        handle::resource buf_output;

        handle::resource buf_indices;

        handle::resource cb_params;
        std::byte* cb_params_map;
    } res;

    struct
    {
        physics_constants_t physics_params;
    } up_data;

    // load compute shader
    {
        auto const sb_compute_cloth = get_shader_binary("res/pr/liveness_sample/shader/bin/cloth/compute_cloth.%s", sample_config.shader_ending);
        CC_RUNTIME_ASSERT(sb_compute_cloth.is_valid() && "failed to load shaders");

        cc::capped_vector<arg::shader_argument_shape, 1> arg_shape;
        {
            arg::shader_argument_shape shape;
            shape.has_cb = true;
            shape.num_srvs = 1;
            shape.num_uavs = 1;
            shape.num_samplers = 0;
            arg_shape.push_back(shape);
        }

        res.pso_compute_cloth = backend.createComputePipelineState(arg_shape, {sb_compute_cloth.get(), sb_compute_cloth.size()}, true);
    }

    // create resources
    {
        res.cb_params = backend.createMappedBuffer(sizeof(physics_constants_t));
        res.cb_params_map = backend.getMappedMemory(res.cb_params);

        up_data.physics_params.init();
        up_data.physics_params.update(0.0005f);
        std::memcpy(res.cb_params_map, &up_data.physics_params, sizeof(up_data.physics_params));

        res.buf_input = backend.createBuffer(lc_cloth_buffer_size, sizeof(particle_t), true);
        res.buf_output = backend.createBuffer(lc_cloth_buffer_size, sizeof(particle_t), true);
    }

    // data upload - particles
    {
        cc::vector<particle_t> particles(lc_cloth_gridsize.width * lc_cloth_gridsize.height);
        {
            float dx = lc_cloth_worldsize.width / (lc_cloth_gridsize.width - 1);
            float dy = lc_cloth_worldsize.height / (lc_cloth_gridsize.height - 1);
            float du = 1.0f / (lc_cloth_gridsize.width - 1);
            float dv = 1.0f / (lc_cloth_gridsize.height - 1);
            auto const transM = tg::translation(tg::pos3(-lc_cloth_worldsize.width / 2.0f, -2.0f, -lc_cloth_worldsize.height / 2.0f));
            for (auto i = 0u; i < lc_cloth_gridsize.height; ++i)
            {
                for (auto j = 0u; j < lc_cloth_gridsize.width; ++j)
                {
                    particles[i + j * lc_cloth_gridsize.height].pos = transM * tg::vec4(dx * j, 0.0f, dy * i, 1.0f);
                    particles[i + j * lc_cloth_gridsize.height].vel = tg::vec4(0.0f);
                    particles[i + j * lc_cloth_gridsize.height].uv = tg::vec4(1.0f - du * i, dv * j, 0.0f, 0.0f);
                }
            }
        }

        auto const staging_buf_handle = backend.createMappedBuffer(lc_cloth_buffer_size);
        auto* const staging_buf_map = backend.getMappedMemory(staging_buf_handle);
        std::memcpy(staging_buf_map, particles.data(), lc_cloth_buffer_size);
        backend.flushMappedMemory(staging_buf_handle);

        pr_test::temp_cmdlist copy_cmdlist(&backend, 1024u);

        cmd::copy_buffer ccmd;
        ccmd.init(staging_buf_handle, res.buf_input, lc_cloth_buffer_size);
        copy_cmdlist.writer.add_command(ccmd);
        ccmd.init(staging_buf_handle, res.buf_output, lc_cloth_buffer_size);
        copy_cmdlist.writer.add_command(ccmd);

        copy_cmdlist.finish();

        backend.free(staging_buf_handle);
    }

    // data upload - indices
    {
        cc::vector<uint32_t> indices;
        indices.reserve(lc_cloth_gridsize.width * lc_cloth_gridsize.height * 2);
        for (auto y = 0u; y < lc_cloth_gridsize.height - 1; ++y)
        {
            for (auto x = 0u; x < lc_cloth_gridsize.width; ++x)
            {
                indices.push_back((y + 1) * lc_cloth_gridsize.width + x);
                indices.push_back(y * lc_cloth_gridsize.width + x);
            }

            // Primitive restart (signalled by special value 0xFFFFFFFF)
            indices.push_back(0xFFFFFFFF);
        }

        auto const index_buffer_bytes = static_cast<unsigned>(indices.size() * sizeof(indices[0]));

        res.buf_indices = backend.createBuffer(index_buffer_bytes, sizeof(indices[0]));

        auto const staging_buf_handle = backend.createMappedBuffer(index_buffer_bytes);
        auto* const staging_buf_map = backend.getMappedMemory(staging_buf_handle);
        std::memcpy(staging_buf_map, indices.data(), index_buffer_bytes);
        backend.flushMappedMemory(staging_buf_handle);

        pr_test::temp_cmdlist copy_cmdlist(&backend, 1024u);

        cmd::copy_buffer ccmd;
        ccmd.init(staging_buf_handle, res.buf_indices, index_buffer_bytes);
        copy_cmdlist.writer.add_command(ccmd);

        copy_cmdlist.finish();

        backend.free(staging_buf_handle);
    }

    {
        shader_view_element sve_a;
        sve_a.init_as_structured_buffer(res.buf_input, lc_num_cloth_particles, sizeof(particle_t));

        shader_view_element sve_b = sve_a;
        sve_b.resource = res.buf_output;

        res.sv_compute_cloth_a = backend.createShaderView(cc::span{sve_a}, cc::span{sve_b}, {}, true);
        res.sv_compute_cloth_b = backend.createShaderView(cc::span{sve_b}, cc::span{sve_a}, {}, true);
    }

    inc::da::Timer timer;
    float run_time = 0.f;
    unsigned framecounter = 440;
    bool pingpong_buf_even = false;

    auto const on_resize_func = [&]() {
        //
    };

    constexpr size_t lc_thread_buffer_size = static_cast<size_t>(10 * 1024u);
    cc::array<std::byte*, pr_test::num_render_threads + 1> thread_cmd_buffer_mem;

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
                    tcmd.add(res.buf_input, resource_state::unordered_access, shader_domain::compute);
                    tcmd.add(res.buf_output, resource_state::unordered_access, shader_domain::compute);
                    cmd_writer.add_command(tcmd);
                }

                {
                    cmd::dispatch dcmd;
                    dcmd.init(res.pso_compute_cloth, lc_cloth_gridsize.width / 10, lc_cloth_gridsize.height / 10, 1);
                    dcmd.add_shader_arg(res.cb_params, 0, handle::null_shader_view);
                    dcmd.write_root_constants(uint32_t(0u));

                    for (auto it = 0u; it < 64u; ++it)
                    {
                        pingpong_buf_even = !pingpong_buf_even;
                        dcmd.shader_arguments[0].shader_view = pingpong_buf_even ? res.sv_compute_cloth_a : res.sv_compute_cloth_b;

                        if (it == 64u - 1)
                        {
                            // compute normals on last iteration
                            dcmd.write_root_constants(uint32_t(1u));
                        }

                        cmd_writer.add_command(dcmd);
                    }
                }

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(res.buf_input, resource_state::vertex_buffer, shader_domain::vertex);
                    tcmd.add(res.buf_output, resource_state::vertex_buffer, shader_domain::vertex);
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
    backend.free(res.pso_compute_cloth);
    backend.free(res.cb_params);
    backend.free(res.buf_input);
    backend.free(res.buf_output);

    window.destroy();
    backend.destroy();
}
