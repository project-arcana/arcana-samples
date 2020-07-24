#include "sample.hh"

#include <atomic>

#include <clean-core/capped_array.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

#include <rich-log/log.hh>

#include <task-dispatcher/td-lean.hh>

#include <phantasm-hardware-interface/commands.hh>
#include <phantasm-hardware-interface/detail/unique_buffer.hh>
#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/window_handle.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>
#include <arcana-incubator/device-abstraction/freefly_camera.hh>
#include <arcana-incubator/device-abstraction/input.hh>
#include <arcana-incubator/device-abstraction/timer.hh>

#include <arcana-incubator/imgui/imgui_impl_phi.hh>
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

struct nbody_camdata
{
    tg::mat4 worldViewProj;
    tg::mat4 invView;
    float _padding[32];
};

static_assert(sizeof(nbody_camdata) == 256 && "camdata misaligned");

constexpr unsigned gc_num_threads = 6;
constexpr unsigned gc_num_particles = 10000;
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

void phi_test::run_nbody_async_compute_sample(phi::Backend& backend, sample_config const& sample_conf, phi::backend_config const& config)
{
    using namespace phi;

    // Basic application init
    // backend init
    backend.initialize(config);

    // window init
    inc::da::initialize();
    inc::da::SDLWindow window;
    window.initialize("phi sample - async compute N-body simulation");

    // main swapchain creation
    phi::handle::swapchain const main_swapchain = backend.createSwapchain({window.getSdlWindow()}, window.getSize());
    unsigned const num_frames = backend.getNumBackbuffers(main_swapchain);
    phi::format const msc_backbuf_format = backend.getBackbufferFormat(main_swapchain);

    // Imgui init
    initialize_imgui(window, backend);

    inc::da::input_manager input;
    input.initialize();
    inc::da::smooth_fps_cam camera;
    camera.setup_default_inputs(input);

    float const num_async_contexts_sqrt = std::sqrt(float(gc_num_threads));
    tg::isize2 num_viewports = {int(std::ceil(num_async_contexts_sqrt)), int(std::ceil(num_async_contexts_sqrt))};
    if (num_viewports.width * (num_viewports.height - 1) >= int(gc_num_threads))
        --num_viewports.height;

    // Resource setup
    struct resources_t
    {
        handle::pipeline_state pso_render;
        handle::pipeline_state pso_compute;

        handle::fence f_global;

        handle::resource b_vertices;

        handle::resource b_camdata_stacked;
        std::byte* map_camdata_stacked;

        struct per_thread_t
        {
            handle::resource b_particle_a;
            handle::resource b_particle_b;

            handle::resource b_particle_a_upload;
            handle::resource b_particle_b_upload;

            handle::shader_view sv_atob;
            handle::shader_view sv_btoa;

            handle::shader_view sv_read_a;
            handle::shader_view sv_read_b;
        };

        cc::capped_array<per_thread_t, 16> threads;
    } res;

    unsigned frame_index = 0;
    uint64_t frame_counter = 0;
    bool atob = true;


    res.f_global = backend.createFence();

    // setup
    {
        inc::growing_writer setup_cmdlist(2048);
        handle::resource upbuffer;

        res.b_camdata_stacked = backend.createUploadBuffer(sizeof(nbody_camdata) * num_frames, sizeof(nbody_camdata));
        res.map_camdata_stacked = backend.mapBuffer(res.b_camdata_stacked);

        // render PSO
        {
            auto render_vs = get_shader_binary("nbody/particle_vs", sample_conf.shader_ending);
            auto render_gs = get_shader_binary("nbody/particle_gs", sample_conf.shader_ending);
            auto render_ps = get_shader_binary("nbody/particle_ps", sample_conf.shader_ending);

            auto const shaders = cc::array{
                arg::graphics_shader{{render_vs.get(), render_vs.size()}, shader_stage::vertex},   //
                arg::graphics_shader{{render_gs.get(), render_gs.size()}, shader_stage::geometry}, //
                arg::graphics_shader{{render_ps.get(), render_ps.size()}, shader_stage::pixel},    //
            };

            auto const vert_attrs = cc::array{vertex_attribute_info{"COLOR", 0, format::rgba32f}};

            arg::framebuffer_config fbconf;
            fbconf.render_targets.push_back(render_target_config{msc_backbuf_format,
                                                                 true,
                                                                 {
                                                                     blend_factor::src_alpha, blend_factor::one, blend_op::op_add, // color
                                                                     blend_factor::zero, blend_factor::zero, blend_op::op_add      // alpha
                                                                 }});

            auto shape = arg::shader_arg_shape(1, 0, 0, true);

            pipeline_config conf;
            conf.topology = primitive_topology::points;

            res.pso_render = backend.createPipelineState({vert_attrs, sizeof(nbody_particle_vertex)}, fbconf, cc::span{shape}, false, shaders, conf);
        }

        // compute PSO
        {
            auto cs = get_shader_binary("nbody/nbody_gravity", sample_conf.shader_ending);
            auto shape = arg::shader_arg_shape(1, 1);
            res.pso_compute = backend.createComputePipelineState(cc::span{shape}, {cs.get(), cs.size()});
        }

        // vertex buffer
        {
            auto particle_data = cc::vector<nbody_particle_vertex>::defaulted(gc_num_particles);
            res.b_vertices = backend.createBuffer(unsigned(particle_data.size_bytes()), sizeof(nbody_particle_vertex));

            upbuffer = backend.createUploadBuffer(unsigned(particle_data.size_bytes()), sizeof(nbody_particle_vertex));
            std::byte* const upbuff_map = backend.mapBuffer(upbuffer);

            std::memcpy(upbuff_map, particle_data.data(), particle_data.size_bytes());

            cmd::transition_resources tcmd;
            tcmd.add(res.b_vertices, resource_state::copy_dest);
            setup_cmdlist.add_command(tcmd);

            cmd::copy_buffer ccmd;
            ccmd.init(upbuffer, res.b_vertices, particle_data.size_bytes(), 0, 0);
            setup_cmdlist.add_command(ccmd);

            tcmd.transitions.clear();
            tcmd.add(res.b_vertices, resource_state::vertex_buffer);
            setup_cmdlist.add_command(tcmd);
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
                thread.b_particle_a = backend.createBuffer(unsigned(data.size_bytes()), sizeof(nbody_particle), resource_heap::gpu, true);
                thread.b_particle_b = backend.createBuffer(unsigned(data.size_bytes()), sizeof(nbody_particle), resource_heap::gpu, true);

                // SVs
                {
                    auto const sv_a = resource_view::structured_buffer(thread.b_particle_a, gc_num_particles, sizeof(nbody_particle));
                    auto const sv_b = resource_view::structured_buffer(thread.b_particle_b, gc_num_particles, sizeof(nbody_particle));

                    thread.sv_atob = backend.createShaderView(cc::span{sv_a}, cc::span{sv_b}, {}, true);
                    thread.sv_btoa = backend.createShaderView(cc::span{sv_b}, cc::span{sv_a}, {}, true);
                    thread.sv_read_a = backend.createShaderView(cc::span{sv_a}, {}, {});
                    thread.sv_read_b = backend.createShaderView(cc::span{sv_b}, {}, {});
                }

                // upload, copy, transitions
                thread.b_particle_a_upload = backend.createUploadBuffer(unsigned(data.size_bytes()), sizeof(nbody_particle));
                thread.b_particle_b_upload = backend.createUploadBuffer(unsigned(data.size_bytes()), sizeof(nbody_particle));

                std::byte* const map_a = backend.mapBuffer(thread.b_particle_a_upload);
                std::byte* const map_b = backend.mapBuffer(thread.b_particle_b_upload);

                std::memcpy(map_a, data.data(), data.size_bytes());
                std::memcpy(map_b, data.data(), data.size_bytes());

                cmd::transition_resources tcmd;
                tcmd.add(thread.b_particle_a, resource_state::copy_dest);
                tcmd.add(thread.b_particle_b, resource_state::copy_dest);
                setup_cmdlist.add_command(tcmd);

                cmd::copy_buffer ccmd;
                ccmd.init(thread.b_particle_a_upload, thread.b_particle_a, data.size_bytes());
                setup_cmdlist.add_command(ccmd);
                ccmd.init(thread.b_particle_b_upload, thread.b_particle_b, data.size_bytes());
                setup_cmdlist.add_command(ccmd);

                tcmd.transitions.clear();
                tcmd.add(thread.b_particle_a, resource_state::shader_resource_nonpixel, shader_stage::compute);
                tcmd.add(thread.b_particle_b, resource_state::shader_resource_nonpixel, shader_stage::compute);
                setup_cmdlist.add_command(tcmd);
            }
        }

        auto const setup_cl = backend.recordCommandList(setup_cmdlist.buffer(), setup_cmdlist.size());
        backend.submit(cc::span{setup_cl});
        ++frame_counter;
        backend.signalFenceGPU(res.f_global, frame_counter, queue_type::direct);
        backend.waitFenceCPU(res.f_global, frame_counter);

        backend.flushGPU();

        backend.free(upbuffer);


        for (auto const& thread : res.threads)
        {
            backend.free(thread.b_particle_a_upload);
            backend.free(thread.b_particle_b_upload);
        }
    }

    float aspect_ratio = 1.f;
    tg::isize2 viewport_size = {};

    auto const f_on_resize = [&] {
        auto const newsize = backend.getBackbufferSize(main_swapchain);

        aspect_ratio = newsize.width / float(newsize.height);
        viewport_size = {newsize.width / num_viewports.width, newsize.height / num_viewports.height};
    };

    f_on_resize();

    cc::array<inc::growing_writer, gc_num_threads> cmdlists_compute;
    inc::growing_writer cmdlist_render;

    inc::da::Timer timer;
    while (!window.isRequestingClose())
    {
        // polling
        input.updatePrePoll();
        SDL_Event e;
        while (window.pollSingleEvent(e))
        {
            input.processEvent(e);
        }
        input.updatePostPoll();

        // resize
        if (window.clearPendingResize())
        {
            if (!window.isMinimized())
                backend.onResize(main_swapchain, window.getSize());
        }

        if (!window.isMinimized())
        {
            auto const frametime = timer.elapsedSeconds();
            timer.restart();

            frame_index = cc::wrapped_increment(frame_index, num_frames);
            atob = !atob;

            if (backend.clearPendingResize(main_swapchain))
                f_on_resize();

            // camera
            {
                camera.update_default_inputs(window, input, frametime);

                auto const view = tg::look_at_directx(camera.physical.position, camera.physical.forward, {0, 1, 0});

                nbody_camdata camdata = {};
                camdata.worldViewProj = tg::perspective_directx(60_deg, aspect_ratio, 1.f, 5000.f) * view;
                camdata.invView = tg::inverse(view);

                std::byte* const dest = res.map_camdata_stacked + sizeof(nbody_camdata) * frame_index;
                std::memcpy(dest, &camdata, sizeof(nbody_camdata));
            }

            cc::array<handle::command_list, gc_num_threads> cls_compute;

            auto compute_sync = td::submit_n(
                [&](unsigned i) {
                    auto& thread = res.threads[i];
                    auto& cmdlist = cmdlists_compute[i];
                    cmdlist.reset();

                    handle::resource uav = atob ? thread.b_particle_b : thread.b_particle_a;
                    handle::shader_view sv = atob ? thread.sv_atob : thread.sv_btoa;

                    cmd::transition_resources tcmd;
                    tcmd.add(uav, resource_state::unordered_access, shader_stage::compute);
                    cmdlist.add_command(tcmd);

                    cmd::dispatch dcmd;
                    dcmd.init(res.pso_compute, int(std::ceil(gc_num_particles / 128.f)), 1, 1);
                    dcmd.add_shader_arg(handle::null_resource, 0, sv);
                    cmdlist.add_command(dcmd);

                    tcmd.transitions.clear();
                    tcmd.add(uav, resource_state::shader_resource_nonpixel, shader_stage::compute);
                    cmdlist.add_command(tcmd);

                    cls_compute[i] = backend.recordCommandList(cmdlist.buffer(), cmdlist.size(), queue_type::compute);
                },
                gc_num_threads);

            // render cmdlist
            handle::command_list cl_render;
            {
                cmdlist_render.reset();

                auto backbuffer = backend.acquireBackbuffer(main_swapchain);
                if (!backbuffer.is_valid())
                    continue;

                cmd::transition_resources tcmd;
                tcmd.add(backbuffer, resource_state::render_target);
                cmdlist_render.add_command(tcmd);

                for (auto i = 0u; i < gc_num_threads; ++i)
                {
                    cmd::begin_render_pass bcmd;
                    bcmd.add_backbuffer_rt(backbuffer, i == 0);
                    bcmd.set_null_depth_stencil();
                    bcmd.viewport_offset = tg::ivec2((i % num_viewports.width) * viewport_size.width, (i / num_viewports.width) * viewport_size.height);
                    bcmd.viewport = viewport_size;
                    cmdlist_render.add_command(bcmd);

                    auto const current_sv = atob ? res.threads[i].sv_read_a : res.threads[i].sv_read_b;

                    cmd::draw dcmd;
                    dcmd.init(res.pso_render, gc_num_particles, res.b_vertices);
                    dcmd.add_shader_arg(res.b_camdata_stacked, sizeof(nbody_camdata) * frame_index, current_sv);
                    cmdlist_render.add_command(dcmd);

                    cmd::end_render_pass ecmd;
                    cmdlist_render.add_command(ecmd);
                }

                ImGui_ImplSDL2_NewFrame(window.getSdlWindow());
                ImGui::NewFrame();

                {
                    ImGui::Begin("N-Body Async Compute Demo");

                    ImGui::Text("Frameime: %.2f ms", frametime * 1000.f);
                    ImGui::Text("%u particles, %u threads,\ndiscrete compute queue", gc_num_particles, gc_num_threads);
                    ImGui::Text("backbuffer %u / %u", frame_index, num_frames);
                    ImGui::Text("cam pos: %.2f %.2f %.2f", double(camera.physical.position.x), double(camera.physical.position.y),
                                double(camera.physical.position.z));
                    ImGui::Text("cam fwd: %.2f %.2f %.2f", double(camera.physical.forward.x), double(camera.physical.forward.y),
                                double(camera.physical.forward.z));

                    ImGui::End();
                }

                ImGui::Render();
                auto* const drawdata = ImGui::GetDrawData();
                auto const commandsize = ImGui_ImplPHI_GetDrawDataCommandSize(drawdata);
                cmdlist_render.accomodate(commandsize);
                ImGui_ImplPHI_RenderDrawData(drawdata, {cmdlist_render.raw_writer().buffer_head(), commandsize});
                cmdlist_render.raw_writer().advance_cursor(commandsize);

                tcmd.transitions.clear();
                tcmd.add(backbuffer, resource_state::present);
                cmdlist_render.add_command(tcmd);

                cl_render = backend.recordCommandList(cmdlist_render.buffer(), cmdlist_render.size(), queue_type::direct);
            }

            td::wait_for(compute_sync);
            backend.submit(cls_compute, queue_type::compute);
            backend.signalFenceGPU(res.f_global, ++frame_counter, queue_type::compute);

            backend.waitFenceGPU(res.f_global, frame_counter, queue_type::direct);
            backend.submit(cc::span{cl_render}, queue_type::direct);
            backend.present(main_swapchain);
        }
    }

    backend.flushGPU();
    shutdown_imgui();
}
