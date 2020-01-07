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

    auto backend_config_cpy = backend_config;
    backend_config_cpy.num_threads = 1;
    backend_config_cpy.max_num_cbvs = 1;
    backend_config_cpy.max_num_srvs = 1;
    backend_config_cpy.max_num_uavs = 1;
    backend_config_cpy.max_num_samplers = 1;
    backend_config_cpy.max_num_resources = 1;
    backend_config_cpy.max_num_pipeline_states = 2;
    backend_config_cpy.num_cmdlist_allocators_per_thread = 1;
    backend_config_cpy.num_cmdlists_per_allocator = 3;
    backend_config_cpy.native_features |= native_feature_flag_bits::vk_api_dump;
    backend.initialize(backend_config_cpy, window);

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

    {
        shader_view_element sve;
        sve.init_as_structured_buffer(res.buf_uav, lc_num_cloth_particles, sizeof(particle_t));
        res.sv_uav = backend.createShaderView({}, cc::span{sve}, {}, true);
    }

    // compute
    {
        pr_test::temp_cmdlist cmdl(&backend, 1024u);

        {
            cmd::transition_resources tcmd;
            tcmd.add(res.buf_uav, resource_state::unordered_access, shader_domain_flag_bits::compute);
            cmdl.writer.add_command(tcmd);
        }

        {
            cmd::dispatch dcmd;
            dcmd.init(res.pso_compute, lc_cloth_gridsize.width / 10, lc_cloth_gridsize.height / 10, 1);
            dcmd.add_shader_arg(handle::null_resource, 0, res.sv_uav);
            cmdl.writer.add_command(dcmd);
        }

        {
            cmd::transition_resources tcmd;
            tcmd.add(res.buf_uav, resource_state::vertex_buffer, shader_domain_flag_bits::vertex);
            cmdl.writer.add_command(tcmd);
        }

        cmdl.finish(true);
    }

    backend.flushGPU();
    backend.free(res.pso_compute);
    backend.free(res.buf_uav);
    backend.free(res.sv_uav);
}
