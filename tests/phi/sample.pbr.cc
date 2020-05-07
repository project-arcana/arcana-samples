#include "sample.hh"

#include <rich-log/log.hh>

#include <clean-core/capped_array.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>
#include <clean-core/utility.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-hardware-interface/commands.hh>
#include <phantasm-hardware-interface/detail/byte_util.hh>
#include <phantasm-hardware-interface/detail/format_size.hh>
#include <phantasm-hardware-interface/detail/unique_buffer.hh>
#include <phantasm-hardware-interface/gpu_info.hh>
#include <phantasm-hardware-interface/window_handle.hh>

#include <phantasm-renderer/reflection/vertex_attributes.hh>

#include <arcana-incubator/asset-loading/image_loader.hh>
#include <arcana-incubator/asset-loading/mesh_loader.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>
#include <arcana-incubator/device-abstraction/timer.hh>

#include <arcana-incubator/imgui/imgui_impl_pr.hh>
#include <arcana-incubator/imgui/imgui_impl_sdl2.hh>
#include <arcana-incubator/imgui/imgui_impl_win32.hh>

#include <arcana-incubator/profiling/remotery.hh>

#include <arcana-incubator/phi-util/mesh_util.hh>
#include <arcana-incubator/phi-util/texture_creation.hh>

#include "sample_util.hh"
#include "scene.hh"

namespace
{
constexpr bool gc_enable_compute_mips = 1;
constexpr unsigned gc_max_num_backbuffers = 4;
constexpr unsigned gc_msaa_samples = 8;
}

void phi_test::run_pbr_sample(phi::Backend& backend, sample_config const& sample_config, const phi::backend_config& backend_config)
{
    inc::RmtInstance _remotery_instance;

    inc::da::initialize();

    using namespace phi;
    CC_RUNTIME_ASSERT(backend_config.num_backbuffers <= gc_max_num_backbuffers && "increase gc_max_num_backbuffers");

    inc::da::SDLWindow window;
    window.initialize(sample_config.window_title);
    backend.initialize(backend_config, {window.getSdlWindow()});

    // Imgui init
    inc::ImGuiPhantasmImpl imgui_implementation;
    initialize_imgui(imgui_implementation, window, backend);

    struct resources_t
    {
        // material
        handle::resource mat_albedo = handle::null_resource;
        handle::resource mat_normal = handle::null_resource;
        handle::resource mat_arm = handle::null_resource;

        // IBL
        handle::resource ibl_specular = handle::null_resource;
        handle::resource ibl_irradiance = handle::null_resource;
        handle::resource ibl_lut = handle::null_resource;

        // mesh
        handle::resource vertex_buffer = handle::null_resource;
        handle::resource index_buffer = handle::null_resource;
        unsigned num_indices = 0;

        // multi-buffered resources
        struct per_frame_resource_t
        {
            phi::handle::resource cb_camdata = phi::handle::null_resource;
            phi::handle::resource sb_modeldata = phi::handle::null_resource;
            phi::handle::resource b_timestamp_readback;

            phi::handle::shader_view shaderview_render_vertex = phi::handle::null_shader_view;
        };

        cc::capped_array<per_frame_resource_t, gc_max_num_backbuffers> per_frame_resources;
        unsigned current_frame_index = 0u;

        per_frame_resource_t const& current_frame() const { return per_frame_resources[current_frame_index]; }

        // render PSO + SVs
        handle::pipeline_state pso_render = handle::null_pipeline_state;
        handle::shader_view shaderview_render = handle::null_shader_view;
        handle::shader_view shaderview_render_ibl = handle::null_shader_view;

        // render targets
        handle::resource depthbuffer = handle::null_resource;
        handle::resource colorbuffer = handle::null_resource;
        handle::resource colorbuffer_resolve = handle::null_resource;

        // blit PSO + SV
        handle::pipeline_state pso_blit = handle::null_pipeline_state;
        handle::shader_view shaderview_blit = handle::null_shader_view;

        // timestamp query range
        handle::query_range timestamp_queries;
    };

    resources_t l_res;

    // Texture setup
    //
    {
        // resource loading, creation and preprocessing
        static_assert(true);
        {
            inc::texture_creator tex_creator;
            tex_creator.initialize(backend, "res/phi/shader/bin");

            l_res.mat_albedo = tex_creator.load_texture(phi_test::sample_albedo_path, format::rgba8un, gc_enable_compute_mips, gc_enable_compute_mips);
            l_res.mat_normal = tex_creator.load_texture(phi_test::sample_normal_path, format::rgba8un, gc_enable_compute_mips, false);
            l_res.mat_arm = tex_creator.load_texture(phi_test::sample_arm_path, format::rgba8un, gc_enable_compute_mips, false);

            l_res.ibl_specular = tex_creator.load_filtered_specular_map("res/arcana-sample-resources/phi/texture/ibl/mono_lake.hdr");
            l_res.ibl_irradiance = tex_creator.create_diffuse_irradiance_map(l_res.ibl_specular);
            l_res.ibl_lut = tex_creator.create_brdf_lut(256);


            tex_creator.free(backend);
        }

        // transitions to SRV
        {
            auto const buffer_size = sizeof(cmd::transition_resources) * 2;
            auto* const buffer = static_cast<std::byte*>(std::malloc(buffer_size));
            CC_DEFER { std::free(buffer); };
            command_stream_writer writer(buffer, buffer_size);

            {
                cmd::transition_resources tcmd;
                tcmd.add(l_res.mat_albedo, resource_state::shader_resource, shader_stage::pixel);
                tcmd.add(l_res.mat_normal, resource_state::shader_resource, shader_stage::pixel);
                tcmd.add(l_res.mat_arm, resource_state::shader_resource, shader_stage::pixel);
                writer.add_command(tcmd);
            }

            {
                cmd::transition_resources tcmd;
                tcmd.add(l_res.ibl_specular, resource_state::shader_resource, shader_stage::pixel);
                tcmd.add(l_res.ibl_irradiance, resource_state::shader_resource, shader_stage::pixel);
                tcmd.add(l_res.ibl_lut, resource_state::shader_resource, shader_stage::pixel);
                writer.add_command(tcmd);
            }

            auto const setup_cmd_list = backend.recordCommandList(writer.buffer(), writer.size());
            backend.submit(cc::span{setup_cmd_list});
        }
    }

    // Mesh setup
    {
        auto const mesh_res = inc::load_mesh(backend, phi_test::sample_mesh_path, phi_test::sample_mesh_binary);

        l_res.num_indices = mesh_res.num_indices;
        l_res.vertex_buffer = mesh_res.vertex_buffer;
        l_res.index_buffer = mesh_res.index_buffer;
    }

    // PSO creation
    {
        cc::array const payload_shape = {
            // Argument 0, global CBV + model mat structured buffer
            arg::shader_arg_shape(1, 0, 0, true),
            // Argument 1, pixel shader SRVs
            arg::shader_arg_shape(3, 0, 1),
            // Argument 2, IBL SRVs and LUT sampler
            arg::shader_arg_shape(3, 0, 1),
        };

        auto const vs = get_shader_binary("mesh_pbr_vs", sample_config.shader_ending);
        auto const ps = get_shader_binary("mesh_pbr_ps", sample_config.shader_ending);
        CC_RUNTIME_ASSERT(vs.is_valid() && ps.is_valid() && "failed to load shaders");

        cc::array const shader_stages
            = {arg::graphics_shader{{vs.get(), vs.size()}, shader_stage::vertex}, arg::graphics_shader{{ps.get(), ps.size()}, shader_stage::pixel}};

        auto const attrib_info = pr::get_vertex_attributes<inc::assets::simple_vertex>();

        arg::framebuffer_config fbconf;
        fbconf.add_render_target(format::rgba16f);
        fbconf.depth_target.push_back(format::depth24un_stencil8u);

        pipeline_config config;
        config.cull = cull_mode::back;
        config.depth = depth_function::less;
        config.samples = gc_msaa_samples;

        l_res.pso_render = backend.createPipelineState(arg::vertex_format{attrib_info, sizeof(inc::assets::simple_vertex)}, fbconf, payload_shape,
                                                       true, shader_stages, config);
    }

    {
        l_res.timestamp_queries = backend.createQueryRange(query_type::timestamp, 2 * backend_config.num_backbuffers);
    }


    {
        // Argument 0, blit target SRV + sampler
        cc::array const payload_shape = {arg::shader_arg_shape(1, 0, 1)};

        auto const vs = get_shader_binary("fullscreen_vs", sample_config.shader_ending);
        auto const ps = get_shader_binary("postprocess_ps", sample_config.shader_ending);
        CC_RUNTIME_ASSERT(vs.is_valid() && ps.is_valid() && "failed to load shaders");

        cc::array const shader_stages
            = {arg::graphics_shader{{vs.get(), vs.size()}, shader_stage::vertex}, arg::graphics_shader{{ps.get(), ps.size()}, shader_stage::pixel}};

        arg::framebuffer_config fbconf;
        fbconf.add_render_target(backend.getBackbufferFormat());

        phi::pipeline_config config;
        config.cull = phi::cull_mode::front;

        l_res.pso_blit = backend.createPipelineState(arg::vertex_format{{}, 0}, fbconf, payload_shape, false, shader_stages, config);
    }

    {
        l_res.per_frame_resources.emplace(backend_config.num_backbuffers);

        auto srv = resource_view::structured_buffer(handle::null_resource, phi_test::num_instances, sizeof(tg::mat4));

        for (auto& pfb : l_res.per_frame_resources)
        {
            pfb.cb_camdata = backend.createUploadBuffer(sizeof(phi_test::global_data));
            pfb.sb_modeldata = backend.createUploadBuffer(sizeof(phi_test::model_matrix_data));

            srv.resource = pfb.sb_modeldata;
            pfb.shaderview_render_vertex = backend.createShaderView(cc::span{srv}, {}, {});

            pfb.b_timestamp_readback = backend.createBuffer(sizeof(uint64_t) * 2, 0, resource_heap::readback);
        }
    }

    {
        sampler_config mat_sampler(sampler_filter::anisotropic);

        cc::array const srv_elems = {resource_view::tex2d(l_res.mat_albedo, format::rgba8un), resource_view::tex2d(l_res.mat_normal, format::rgba8un),
                                     resource_view::tex2d(l_res.mat_arm, format::rgba8un)};

        l_res.shaderview_render = backend.createShaderView(srv_elems, {}, cc::span{mat_sampler});
    }

    {
        sampler_config lut_sampler(sampler_filter::min_mag_mip_linear, 1);
        lut_sampler.address_u = sampler_address_mode::clamp;
        lut_sampler.address_v = sampler_address_mode::clamp;

        cc::array const srv_elems = {resource_view::texcube(l_res.ibl_specular, format::rgba16f),
                                     resource_view::texcube(l_res.ibl_irradiance, format::rgba16f), resource_view::tex2d(l_res.ibl_lut, format::rg16f)};

        l_res.shaderview_render_ibl = backend.createShaderView(srv_elems, {}, cc::span{lut_sampler});
    }

    tg::isize2 backbuf_size = tg::isize2(150, 150);

    auto const f_create_sized_resources = [&] {
        l_res.depthbuffer = backend.createRenderTarget(format::depth24un_stencil8u, backbuf_size, gc_msaa_samples);
        l_res.colorbuffer = backend.createRenderTarget(format::rgba16f, backbuf_size, gc_msaa_samples);
        l_res.colorbuffer_resolve = gc_msaa_samples > 1 ? backend.createTexture(format::rgba16f, backbuf_size, 1) : l_res.colorbuffer;

        {
            auto const sampler = sampler_config(sampler_filter::min_mag_mip_point);
            auto const srv = resource_view::tex2d(l_res.colorbuffer_resolve, format::rgba16f);
            l_res.shaderview_blit = backend.createShaderView(cc::span{srv}, {}, cc::span{sampler});
        }

        {
            std::byte writer_mem[sizeof(cmd::transition_resources)];
            command_stream_writer writer(writer_mem, sizeof(writer_mem));

            cmd::transition_resources transition_cmd;
            transition_cmd.add(l_res.depthbuffer, resource_state::depth_write);
            transition_cmd.add(l_res.colorbuffer, resource_state::render_target);
            writer.add_command(transition_cmd);

            auto const cl = backend.recordCommandList(writer.buffer(), writer.size());
            backend.submit(cc::span{cl});
        }
    };

    auto const f_destroy_sized_resources = [&] {
        backend.free(l_res.depthbuffer);
        backend.free(l_res.colorbuffer);
        if constexpr (gc_msaa_samples > 1)
        {
            backend.free(l_res.colorbuffer_resolve);
        }

        backend.free(l_res.shaderview_blit);
    };

    auto const f_on_resize = [&]() {
        backend.flushGPU();
        backbuf_size = backend.getBackbufferSize();
        LOG("backbuffer resized to {}x{}", backbuf_size.width, backbuf_size.height);
        f_destroy_sized_resources();
        f_create_sized_resources();
    };

    // initial create
    f_create_sized_resources();

    // Main loop
    inc::da::Timer timer;
    float run_time = 0.f;

    tg::vec3 position_modulos = tg::vec3(9, 6, 9);
    float camera_distance = 1.f;

// cacheline-sized tasks call for desperate measures (macro)
#define THREAD_BUFFER_SIZE (static_cast<size_t>((sizeof(cmd::draw) * (phi_test::num_instances / phi_test::num_render_threads)) + 1024u))

    cc::array<std::byte*, phi_test::num_render_threads + 1> thread_cmd_buffer_mem;

    for (auto& mem : thread_cmd_buffer_mem)
        mem = static_cast<std::byte*>(std::malloc(THREAD_BUFFER_SIZE));

    CC_DEFER
    {
        for (std::byte* mem : thread_cmd_buffer_mem)
            std::free(mem);
    };

    phi_test::model_matrix_data* model_data = new phi_test::model_matrix_data();
    CC_DEFER { delete model_data; };

    uint64_t last_gpu_delta = 0;
    uint64_t const gpu_timestamp_frequency = backend.getGPUTimestampFrequency();

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
            INC_RMT_TRACE_NAMED("CPUFrame");

            auto const frametime = timer.elapsedMilliseconds();
            timer.restart();
            run_time += frametime / 1000.f;

            l_res.current_frame_index = cc::wrapped_increment(l_res.current_frame_index, backend_config.num_backbuffers);

            if (backend.clearPendingResize())
                f_on_resize();

            // 1 per frame, plus postprocessing and UI
            cc::array<handle::command_list, phi_test::num_render_threads + 1> all_command_lists;
            cc::fill(all_command_lists, handle::null_command_list);

            td::sync render_sync, modeldata_upload_sync;
            // parallel rendering
            {
                INC_RMT_TRACE_NAMED("TaskDispatch");

                td::submit_batched_n(
                    render_sync,
                    [&](unsigned start, unsigned end, unsigned i) {
                        INC_RMT_TRACE_NAMED("CommandRecordTask");

                        command_stream_writer cmd_writer(thread_cmd_buffer_mem[i + 1], THREAD_BUFFER_SIZE);

                        auto const is_first_batch = i == 0;
                        auto const clear_or_load = is_first_batch ? rt_clear_type::clear : rt_clear_type::load;

                        if (is_first_batch)
                        {
                            cmd::transition_resources tcmd;
                            tcmd.add(l_res.colorbuffer, resource_state::render_target);
                            cmd_writer.add_command(tcmd);

                            // write the frame-start timestamp (even index)
                            cmd::write_timestamp wtcmd(l_res.timestamp_queries, 0 + l_res.current_frame_index * 2);
                            cmd_writer.add_command(wtcmd);
                        }
                        {
                            cmd::begin_render_pass bcmd;
                            bcmd.viewport = backend.getBackbufferSize();
                            bcmd.add_2d_rt(l_res.colorbuffer, format::rgba16f, clear_or_load, gc_msaa_samples > 1);
                            bcmd.set_2d_depth_stencil(l_res.depthbuffer, format::depth24un_stencil8u, clear_or_load, gc_msaa_samples > 1);
                            cmd_writer.add_command(bcmd);
                        }

                        {
                            cmd::draw dcmd;
                            dcmd.init(l_res.pso_render, l_res.num_indices, l_res.vertex_buffer, l_res.index_buffer);
                            dcmd.add_shader_arg(l_res.current_frame().cb_camdata, 0, l_res.current_frame().shaderview_render_vertex);
                            dcmd.add_shader_arg(handle::null_resource, 0, l_res.shaderview_render);
                            dcmd.add_shader_arg(handle::null_resource, 0, l_res.shaderview_render_ibl);

                            for (auto inst = start; inst < end; ++inst)
                            {
                                dcmd.write_root_constants(static_cast<unsigned>(inst));
                                cmd_writer.add_command(dcmd);
                            }
                        }

                        cmd_writer.add_command(cmd::end_render_pass{});

                        {
                            INC_RMT_TRACE_NAMED("CommandRecordTaskBackend");
                            all_command_lists[i] = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
                        }
                    },
                    phi_test::num_instances, phi_test::num_render_threads);


                td::submit_batched(
                    modeldata_upload_sync,
                    [run_time, model_data, position_modulos](unsigned start, unsigned end) {
                        INC_RMT_TRACE_NAMED("ModelMatrixTask");
                        phi_test::fill_model_matrix_data(*model_data, run_time, start, end, position_modulos);
                    },
                    phi_test::num_instances, phi_test::num_render_threads);
            }

            {
                command_stream_writer cmd_writer(thread_cmd_buffer_mem[0], THREAD_BUFFER_SIZE);

                auto const current_backbuffer = backend.acquireBackbuffer();

                if (!current_backbuffer.is_valid())
                {
                    // The vulkan-only scenario: acquiring failed, and we have to discard the current frame
                    td::wait_for(render_sync, modeldata_upload_sync);
                    backend.discard(all_command_lists);
                    continue;
                }

                if constexpr (gc_msaa_samples > 1)
                {
                    {
                        cmd::transition_resources tcmd;
                        tcmd.add(l_res.colorbuffer, resource_state::resolve_src);
                        tcmd.add(l_res.colorbuffer_resolve, resource_state::resolve_dest);
                        cmd_writer.add_command(tcmd);
                    }

                    {
                        cmd::resolve_texture rcmd;
                        rcmd.init_symmetric(l_res.colorbuffer, l_res.colorbuffer_resolve, backbuf_size.width, backbuf_size.height, 0);
                        cmd_writer.add_command(rcmd);
                    }
                }

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(current_backbuffer, resource_state::render_target);
                    tcmd.add(l_res.colorbuffer_resolve, resource_state::shader_resource, shader_stage::pixel);
                    cmd_writer.add_command(tcmd);
                }

                {
                    cmd::begin_render_pass bcmd;
                    bcmd.viewport = backend.getBackbufferSize();
                    bcmd.add_backbuffer_rt(current_backbuffer);
                    bcmd.set_null_depth_stencil();
                    cmd_writer.add_command(bcmd);
                }

                {
                    cmd::draw dcmd;
                    dcmd.init(l_res.pso_blit, 3);
                    dcmd.add_shader_arg(handle::null_resource, 0, l_res.shaderview_blit);
                    cmd_writer.add_command(dcmd);
                }

                cmd_writer.add_command(cmd::end_render_pass{});

                // ImGui and transition to present
                {
                    INC_RMT_TRACE_NAMED("ImGuiRecord");
                    ImGui_ImplSDL2_NewFrame(window.getSdlWindow());
                    ImGui::NewFrame();

                    {
                        ImGui::Begin("PBR Demo");

                        ImGui::Text("Frametime: %.3f ms\nGPU Time: %.3f ms\nGPU timestamp delta: %llu @ %llu Hz", frametime,
                                    (double(last_gpu_delta) / gpu_timestamp_frequency) * 1000., last_gpu_delta, gpu_timestamp_frequency);
                        ImGui::SliderFloat3("Position modulos", tg::data_ptr(position_modulos), 1.f, 50.f);
                        ImGui::SliderFloat("Camera Distance", &camera_distance, 1.f, 15.f, "%.3f", 2.f);

                        if (ImGui::Button("Reset modulos"))
                            position_modulos = tg::vec3(9, 6, 9);

                        if (ImGui::Button("Reset runtime"))
                            run_time = 0.f;

                        ImGui::End();
                    }


                    ImGui::Render();
                    auto* const drawdata = ImGui::GetDrawData();
                    auto const commandsize = imgui_implementation.get_command_size(drawdata);
                    imgui_implementation.write_commands(drawdata, current_backbuffer, cmd_writer.buffer_head(), commandsize);
                    cmd_writer.advance_cursor(commandsize);
                }

                // transition backbuffer to present
                cmd::transition_resources tcmd;
                tcmd.add(current_backbuffer, resource_state::present);
                cmd_writer.add_command(tcmd);

                // write the frame-end timestamp (odd index)
                cmd::write_timestamp wtcmd(l_res.timestamp_queries, 1 + l_res.current_frame_index * 2);
                cmd_writer.add_command(wtcmd);

                // resolve the timestamps
                {
                    cmd::resolve_queries rcmd;
                    rcmd.init(l_res.current_frame().b_timestamp_readback, l_res.timestamp_queries, l_res.current_frame_index * 2, 2);
                    cmd_writer.add_command(rcmd);
                }

                // fill in last commandlist
                all_command_lists[phi_test::num_render_threads] = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
            }

            // Data upload
            {
                INC_RMT_TRACE_NAMED("DataUpload");
                phi_test::global_data camdata;
                camdata.cam_pos = phi_test::get_cam_pos(run_time, camera_distance);
                camdata.cam_vp = phi_test::get_view_projection_matrix(camdata.cam_pos, window.getWidth(), window.getHeight());
                camdata.runtime = static_cast<float>(run_time);

                auto* const camdata_map = backend.mapBuffer(l_res.current_frame().cb_camdata);
                std::memcpy(camdata_map, &camdata, sizeof(camdata));
                backend.unmapBuffer(l_res.current_frame().cb_camdata);

                td::wait_for(modeldata_upload_sync);
                auto* const modeldata_map = backend.mapBuffer(l_res.current_frame().sb_modeldata);
                std::memcpy(modeldata_map, model_data, sizeof(phi_test::model_matrix_data));
                backend.unmapBuffer(l_res.current_frame().sb_modeldata);
            }

            // CPU-sync and submit
            td::wait_for(render_sync);
            backend.submit(all_command_lists);

            // present
            {
                INC_RMT_TRACE_NAMED("Present");
                backend.present();
            }

            // map and memcpy from readback buffer of _next_ frame (or the one most distant from now)
            {
                handle::resource const readback_buf
                    = l_res.per_frame_resources[cc::wrapped_increment(l_res.current_frame_index, backend_config.num_backbuffers)].b_timestamp_readback;

                auto* const readback_map = backend.mapBuffer(readback_buf);

                struct
                {
                    uint64_t time_begin;
                    uint64_t time_end;
                } value;

                std::memcpy(&value, readback_map, sizeof(value));

                last_gpu_delta = value.time_end - value.time_begin;

                backend.unmapBuffer(readback_buf);
            }
        }
    }

    backend.flushGPU();

    // free all resources at once
    cc::array const free_batch
        = {l_res.mat_albedo,    l_res.mat_normal,   l_res.mat_arm,     l_res.ibl_lut,     l_res.ibl_specular,       l_res.ibl_irradiance,
           l_res.vertex_buffer, l_res.index_buffer, l_res.colorbuffer, l_res.depthbuffer, l_res.colorbuffer_resolve};
    backend.freeRange(free_batch);

    // free other objects
    backend.freeVariadic(l_res.shaderview_render_ibl, l_res.pso_render, l_res.pso_blit, l_res.shaderview_blit, l_res.shaderview_render);
    for (auto const& pfr : l_res.per_frame_resources)
        backend.freeVariadic(pfr.cb_camdata, pfr.sb_modeldata, pfr.shaderview_render_vertex, pfr.b_timestamp_readback);

    shutdown_imgui(imgui_implementation);

    backend.destroy();
    window.destroy();
    inc::da::shutdown();
}
