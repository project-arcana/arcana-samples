#include "sample.hh"

#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-hardware-interface/commands.hh>
#include <phantasm-hardware-interface/detail/unique_buffer.hh>
#include <phantasm-hardware-interface/window_handle.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>
#include <arcana-incubator/device-abstraction/timer.hh>
#include <arcana-incubator/imgui/imgui_impl_pr.hh>
#include <arcana-incubator/imgui/imgui_impl_sdl2.hh>
#include <arcana-incubator/phi-util/growing_writer.hh>

#include "sample_util.hh"

namespace
{
struct nbody_particle_vertex
{
    tg::vec4 color = {1, 1, 0.2f, 1};
};

struct nbody_particle
{
    tg::vec4 position;
    tg::vec4 velocity;
};

constexpr unsigned gc_num_threads = 5;
constexpr unsigned gc_num_particles = 1000;
constexpr float gc_particle_spread = 400.f;

void initialize_nbody_particle_data(cc::span<nbody_particle> out_particles, tg::pos3 center, tg::vec4 velocity, float spread)
{
    auto const f_random_percent = [] {
        float ret = static_cast<float>((::rand() % 10000) - 5000);
        return ret / 5000.0f;
    };

    ::srand(0);
    for (nbody_particle& part : out_particles)
    {
        auto delta = tg::vec3{spread, spread, spread};

        while (tg::length_sqr(delta) > spread * spread)
            delta = {f_random_percent() * spread, f_random_percent() * spread, f_random_percent() * spread};

        part.position = {center.x + delta.x, center.y + delta.y, center.z + delta.z, 10000.0f * 10000.0f};
        part.velocity = velocity;
    }
}
}

void phi_test::run_nbody_async_compute_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& config)
{
    using namespace phi;

    // Basic init
    inc::da::SDLWindow window;
    window.initialize("phi sample - async compute N-body simulation");
    backend.initialize(config, window_handle{window.getSdlWindow()});
    inc::ImGuiPhantasmImpl imgui_impl;
    initialize_imgui(imgui_impl, window, backend);

    // Resource setup
    struct resources_t
    {
        handle::pipeline_state pso_render;
        handle::pipeline_state pso_compute;

        handle::fence f_setup;

        handle::resource b_vertices;

        struct per_thread_t
        {
            handle::resource b_particle_a;
            handle::resource b_particle_b;

            handle::resource b_particle_a_upload;
            handle::resource b_particle_b_upload;
        };

        cc::capped_array<per_thread_t, 16> threads;
    } res;

    res.f_setup = backend.createFence();

    // setup
    {
        inc::growing_writer setup_cmdlist(2048);
        handle::resource upbuffer;

        // vertex buffer
        {
            auto particle_data = cc::vector<nbody_particle_vertex>::defaulted(gc_num_particles);
            res.b_vertices = backend.createBuffer(particle_data.size_bytes(), sizeof(nbody_particle_vertex));

            upbuffer = backend.createMappedBuffer(particle_data.size_bytes(), sizeof(nbody_particle_vertex));
            std::byte* const upbuff_map = backend.getMappedMemory(upbuffer);

            std::memcpy(upbuff_map, particle_data.data(), particle_data.size_bytes());

            cmd::copy_buffer ccmd;
            ccmd.init(upbuffer, res.b_vertices, particle_data.size_bytes(), 0, 0);
            setup_cmdlist.add_command(ccmd);
        }

        // per-thread particle buffers
        {
            CC_RUNTIME_ASSERT(gc_num_threads <= config.num_threads && "too many threads configured");
            CC_RUNTIME_ASSERT(gc_num_threads < res.threads.max_size() && "too many threads configured");

            // this slightly esoteric setup was copied 1:1 from the d3d12 sample for reproduction's sake
            auto data = cc::vector<nbody_particle>::defaulted(gc_num_particles);
            float const center_spread = gc_particle_spread * .5f;
            initialize_nbody_particle_data(cc::span{data}.subspan(0, gc_num_particles / 2), {center_spread, 0, 0}, {0, 0, -20, 1 / 100000000.0f}, gc_particle_spread);
            initialize_nbody_particle_data(cc::span{data}.subspan(gc_num_particles / 2, gc_num_particles / 2), {-center_spread, 0, 0},
                                           {0, 0, 20, 1 / 100000000.0f}, gc_particle_spread);

            res.threads.emplace(gc_num_threads);
            for (auto& thread : res.threads)
            {
                thread.b_particle_a = backend.createBuffer(data.size_bytes(), sizeof(nbody_particle), true);
                thread.b_particle_b = backend.createBuffer(data.size_bytes(), sizeof(nbody_particle), true);

                thread.b_particle_a_upload = backend.createMappedBuffer(data.size_bytes(), sizeof(nbody_particle));
                thread.b_particle_b_upload = backend.createMappedBuffer(data.size_bytes(), sizeof(nbody_particle));
            }
        }

        auto const setup_cl = backend.recordCommandList(setup_cmdlist.buffer(), setup_cmdlist.size());
        backend.submit(cc::span{setup_cl});
        backend.signalFenceGPU(res.f_setup, 1, queue_type::direct);
        backend.waitFenceCPU(res.f_setup, 1);

        backend.free(upbuffer);
    }

    inc::da::Timer timer;
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
        }
    }

    backend.flushGPU();
    shutdown_imgui(imgui_impl);
}
