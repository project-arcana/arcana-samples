#include "sample.hh"

#include <rich-log/log.hh>

#include <clean-core/capped_array.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>

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
#include <arcana-incubator/device-abstraction/timer.hh>
#include <arcana-incubator/device-abstraction/window.hh>
#include <arcana-incubator/imgui/imgui_impl_pr.hh>
#include <arcana-incubator/imgui/imgui_impl_win32.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>
#include <arcana-incubator/imgui/imgui_impl_sdl2.hh>

#include "mip_generation.hh"
#include "scene.hh"
#include "temp_cmdlist.hh"


namespace
{
constexpr bool gc_enable_compute_mips = 1;
constexpr unsigned gc_max_num_backbuffers = 4;
constexpr unsigned gc_msaa_samples = 8;
}

void phi_test::run_pbr_sample(phi::Backend& backend, sample_config const& sample_config, const phi::backend_config& backend_config)
{
    inc::da::initialize();

    using namespace phi;
    CC_RUNTIME_ASSERT(backend_config.num_backbuffers <= gc_max_num_backbuffers && "increase gc_max_num_backbuffers");

    inc::da::SDLWindow window;
    window.initialize(sample_config.window_title);
    backend.initialize(backend_config, {window.getSdlWindow()});

    // Imgui init
    inc::ImGuiPhantasmImpl imgui_implementation;
    {
        ImGui::SetCurrentContext(ImGui::CreateContext(nullptr));
        ImGui_ImplSDL2_Init(window.getSdlWindow());
        {
            auto const ps_bin = get_shader_binary("imgui_ps", sample_config.shader_ending);
            auto const vs_bin = get_shader_binary("imgui_vs", sample_config.shader_ending);
            imgui_implementation.init(&backend, backend.getNumBackbuffers(), ps_bin.get(), ps_bin.size(), vs_bin.get(), vs_bin.size(), sample_config.align_mip_rows);
        }
    }

    struct resources_t
    {
        // material
        handle::resource mat_albedo = handle::null_resource;
        handle::resource mat_normal = handle::null_resource;
        handle::resource mat_metallic = handle::null_resource;
        handle::resource mat_roughness = handle::null_resource;

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
            std::byte* cb_camdata_map = nullptr;
            std::byte* sb_modeldata_map = nullptr;

            phi::handle::shader_view shaderview_render_vertex = phi::handle::null_shader_view;
        };

        cc::capped_array<per_frame_resource_t, gc_max_num_backbuffers> per_frame_resources;
        unsigned current_frame_index = 0u;

        per_frame_resource_t const& current_frame() const { return per_frame_resources[current_frame_index]; }

        // render PSO + SVs
        handle::pipeline_state pso_render = handle::null_pipeline_state;
        handle::shader_view shaderview_render = handle::null_shader_view;
        handle::shader_view shaderview_render_ibl = handle::null_shader_view;

        // render RTs
        handle::resource depthbuffer = handle::null_resource;
        handle::resource colorbuffer = handle::null_resource;
        handle::resource colorbuffer_resolve = handle::null_resource;

        // blit PSO + SV
        handle::pipeline_state pso_blit = handle::null_pipeline_state;
        handle::shader_view shaderview_blit = handle::null_shader_view;
    };

    resources_t l_res;

    // Texture setup
    //
    {
        // resource loading, creation and preprocessing
        static_assert(true, "clang-format");
        {
            phi_test::texture_creation_resources texgen_resources;
            texgen_resources.initialize(backend, sample_config.shader_ending, sample_config.align_mip_rows);

            l_res.mat_albedo = texgen_resources.load_texture(phi_test::sample_albedo_path, format::rgba8un, gc_enable_compute_mips, gc_enable_compute_mips);
            l_res.mat_normal = texgen_resources.load_texture(phi_test::sample_normal_path, format::rgba8un, gc_enable_compute_mips, false);
            l_res.mat_metallic = texgen_resources.load_texture(phi_test::sample_metallic_path, format::r8un, gc_enable_compute_mips, false);
            l_res.mat_roughness = texgen_resources.load_texture(phi_test::sample_roughness_path, format::r8un, gc_enable_compute_mips, false);

            l_res.ibl_specular = texgen_resources.load_filtered_specular_map("res/pr/liveness_sample/texture/ibl/mono_lake.hdr");
            l_res.ibl_irradiance = texgen_resources.create_diffuse_irradiance_map(l_res.ibl_specular);
            l_res.ibl_lut = texgen_resources.create_brdf_lut(256);


            texgen_resources.free(backend);
        }

        // transitions to SRV
        {
            auto const buffer_size = sizeof(cmd::transition_resources) * 2;
            auto* const buffer = static_cast<std::byte*>(std::malloc(buffer_size));
            CC_DEFER { std::free(buffer); };
            command_stream_writer writer(buffer, buffer_size);

            {
                cmd::transition_resources tcmd;
                tcmd.add(l_res.mat_albedo, resource_state::shader_resource, shader_domain::pixel);
                tcmd.add(l_res.mat_normal, resource_state::shader_resource, shader_domain::pixel);
                tcmd.add(l_res.mat_metallic, resource_state::shader_resource, shader_domain::pixel);
                tcmd.add(l_res.mat_roughness, resource_state::shader_resource, shader_domain::pixel);
                writer.add_command(tcmd);
            }

            {
                cmd::transition_resources tcmd;
                tcmd.add(l_res.ibl_specular, resource_state::shader_resource, shader_domain::pixel);
                tcmd.add(l_res.ibl_irradiance, resource_state::shader_resource, shader_domain::pixel);
                tcmd.add(l_res.ibl_lut, resource_state::shader_resource, shader_domain::pixel);
                writer.add_command(tcmd);
            }

            auto const setup_cmd_list = backend.recordCommandList(writer.buffer(), writer.size());
            backend.submit(cc::span{setup_cmd_list});
        }
    }

    // Mesh setup
    {
        handle::resource upload_buffer;

        auto const buffer_size = 1024ull * 2;
        auto* const buffer = static_cast<std::byte*>(std::malloc(buffer_size));
        CC_DEFER { std::free(buffer); };
        command_stream_writer writer(buffer, buffer_size);

        {
            auto const mesh_data = phi_test::sample_mesh_binary ? inc::assets::load_binary_mesh(phi_test::sample_mesh_path)
                                                                : inc::assets::load_obj_mesh(phi_test::sample_mesh_path);

            l_res.num_indices = unsigned(mesh_data.indices.size());

            auto const vert_size = mesh_data.get_vertex_size_bytes();
            auto const ind_size = mesh_data.get_index_size_bytes();

            l_res.vertex_buffer = backend.createBuffer(vert_size, sizeof(inc::assets::simple_vertex));
            l_res.index_buffer = backend.createBuffer(ind_size, sizeof(int));

            {
                cmd::transition_resources tcmd;
                tcmd.add(l_res.vertex_buffer, resource_state::copy_dest);
                tcmd.add(l_res.index_buffer, resource_state::copy_dest);
                writer.add_command(tcmd);
            }

            upload_buffer = backend.createMappedBuffer(vert_size + ind_size);
            std::byte* const upload_mapped = backend.getMappedMemory(upload_buffer);

            std::memcpy(upload_mapped, mesh_data.vertices.data(), vert_size);
            std::memcpy(upload_mapped + vert_size, mesh_data.indices.data(), ind_size);

            writer.add_command(cmd::copy_buffer{l_res.vertex_buffer, 0, upload_buffer, 0, vert_size});
            writer.add_command(cmd::copy_buffer{l_res.index_buffer, 0, upload_buffer, vert_size, ind_size});

            {
                cmd::transition_resources tcmd;
                tcmd.add(l_res.vertex_buffer, resource_state::vertex_buffer);
                tcmd.add(l_res.index_buffer, resource_state::index_buffer);
                writer.add_command(tcmd);
            }
        }

        auto const setup_cmd_list = backend.recordCommandList(writer.buffer(), writer.size());

        backend.flushMappedMemory(upload_buffer);
        backend.submit(cc::span{setup_cmd_list});
        backend.flushGPU();
        backend.free(upload_buffer);
    }

    {
        cc::array const payload_shape = {
            // Argument 0, global CBV + model mat structured buffer
            arg::shader_arg_shape(1, 0, 0, true),
            // Argument 1, pixel shader SRVs
            arg::shader_arg_shape(4, 0, 1),
            // Argument 2, IBL SRVs and LUT sampler
            arg::shader_arg_shape(3, 0, 1),
        };

        auto const vs = get_shader_binary("vertex", sample_config.shader_ending);
        auto const ps = get_shader_binary("pixel", sample_config.shader_ending);
        CC_RUNTIME_ASSERT(vs.is_valid() && ps.is_valid() && "failed to load shaders");

        cc::array const shader_stages
            = {arg::shader_stage{{vs.get(), vs.size()}, shader_domain::vertex}, arg::shader_stage{{ps.get(), ps.size()}, shader_domain::pixel}};

        auto const attrib_info = pr::get_vertex_attributes<inc::assets::simple_vertex>();

        arg::framebuffer_config fbconf;
        fbconf.add_render_target(format::rgba16f);
        fbconf.depth_target.push_back(format::depth24un_stencil8u);

        graphics_pipeline_config config;
        config.samples = gc_msaa_samples;

        l_res.pso_render = backend.createPipelineState(arg::vertex_format{attrib_info, sizeof(inc::assets::simple_vertex)}, fbconf, payload_shape,
                                                       true, shader_stages, config);
    }

    {
        // Argument 0, blit target SRV + sampler
        cc::array const payload_shape = {arg::shader_arg_shape(1, 0, 1)};

        auto const vs = get_shader_binary("blit_vertex", sample_config.shader_ending);
        auto const ps = get_shader_binary("blit_pixel", sample_config.shader_ending);
        CC_RUNTIME_ASSERT(vs.is_valid() && ps.is_valid() && "failed to load shaders");

        cc::array const shader_stages
            = {arg::shader_stage{{vs.get(), vs.size()}, shader_domain::vertex}, arg::shader_stage{{ps.get(), ps.size()}, shader_domain::pixel}};

        arg::framebuffer_config fbconf;
        fbconf.add_render_target(backend.getBackbufferFormat());

        phi::graphics_pipeline_config config;
        config.cull = phi::cull_mode::front;

        l_res.pso_blit = backend.createPipelineState(arg::vertex_format{{}, 0}, fbconf, payload_shape, false, shader_stages, config);
    }

    {
        l_res.per_frame_resources.emplace(backend_config.num_backbuffers);

        shader_view_elem srv;
        srv.init_as_structured_buffer(handle::null_resource, phi_test::num_instances, sizeof(tg::mat4));

        for (auto& pfb : l_res.per_frame_resources)
        {
            pfb.cb_camdata = backend.createMappedBuffer(sizeof(phi_test::global_data));
            pfb.cb_camdata_map = backend.getMappedMemory(pfb.cb_camdata);

            pfb.sb_modeldata = backend.createMappedBuffer(sizeof(phi_test::model_matrix_data));
            pfb.sb_modeldata_map = backend.getMappedMemory(pfb.sb_modeldata);

            srv.resource = pfb.sb_modeldata;
            pfb.shaderview_render_vertex = backend.createShaderView(cc::span{srv}, {}, {});
        }
    }

    {
        sampler_config mat_sampler(sampler_filter::anisotropic);

        cc::array const srv_elems = {shader_view_elem::tex2d(l_res.mat_albedo, format::rgba8un), shader_view_elem::tex2d(l_res.mat_normal, format::rgba8un),
                                     shader_view_elem::tex2d(l_res.mat_metallic, format::r8un), shader_view_elem::tex2d(l_res.mat_roughness, format::r8un)};

        l_res.shaderview_render = backend.createShaderView(srv_elems, {}, cc::span{mat_sampler});
    }

    {
        sampler_config lut_sampler(sampler_filter::min_mag_mip_linear, 1);
        lut_sampler.address_u = sampler_address_mode::clamp;
        lut_sampler.address_v = sampler_address_mode::clamp;

        cc::array const srv_elems = {shader_view_elem::texcube(l_res.ibl_specular, format::rgba16f),
                                     shader_view_elem::texcube(l_res.ibl_irradiance, format::rgba16f), shader_view_elem::tex2d(l_res.ibl_lut, format::rg16f)};

        l_res.shaderview_render_ibl = backend.createShaderView(srv_elems, {}, cc::span{lut_sampler});
    }

    tg::isize2 backbuf_size = tg::isize2(150, 150);

    auto const f_create_sized_resources = [&] {
        auto const w = static_cast<unsigned>(backbuf_size.width);
        auto const h = static_cast<unsigned>(backbuf_size.height);

        l_res.depthbuffer = backend.createRenderTarget(format::depth24un_stencil8u, w, h, gc_msaa_samples);
        l_res.colorbuffer = backend.createRenderTarget(format::rgba16f, w, h, gc_msaa_samples);
        l_res.colorbuffer_resolve = gc_msaa_samples > 1 ? backend.createTexture(format::rgba16f, w, h, 1) : l_res.colorbuffer;

        {
            sampler_config rt_sampler;
            rt_sampler.init_default(sampler_filter::min_mag_mip_point);

            cc::capped_vector<shader_view_elem, 1> srv_elems;
            srv_elems.emplace_back().init_as_tex2d(l_res.colorbuffer_resolve, format::rgba16f);
            l_res.shaderview_blit = backend.createShaderView(srv_elems, {}, cc::span{rt_sampler});
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

    auto const on_resize_func = [&]() {
        backend.flushGPU();
        backbuf_size = backend.getBackbufferSize();
        LOG(info)("backbuffer resized to {}x{}", backbuf_size.width, backbuf_size.height);
        f_destroy_sized_resources();
        f_create_sized_resources();
    };

    // initial create
    f_create_sized_resources();

    // Main loop
    inc::da::Timer timer;
    float run_time = 0.f;
    float log_time = 0.f;

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
                LOG(info)("frametime: {}ms", frametime);
            }

            ++l_res.current_frame_index;
            if (l_res.current_frame_index >= backend_config.num_backbuffers)
                l_res.current_frame_index -= backend_config.num_backbuffers;

            if (backend.clearPendingResize())
                on_resize_func();

            cc::array<handle::command_list, phi_test::num_render_threads> render_cmd_lists;
            cc::fill(render_cmd_lists, handle::null_command_list);

            // parallel rendering
            auto render_sync = td::submit_batched_n(
                [&](unsigned start, unsigned end, unsigned i) {
                    command_stream_writer cmd_writer(thread_cmd_buffer_mem[i + 1], THREAD_BUFFER_SIZE);

                    auto const is_first_batch = i == 0;
                    auto const clear_or_load = is_first_batch ? rt_clear_type::clear : rt_clear_type::load;

                    if (is_first_batch)
                    {
                        cmd::transition_resources cmd_trans;
                        cmd_trans.add(l_res.colorbuffer, resource_state::render_target);
                        cmd_writer.add_command(cmd_trans);
                    }
                    {
                        cmd::begin_render_pass cmd_brp;
                        cmd_brp.viewport = backend.getBackbufferSize();
                        cmd_brp.add_2d_rt(l_res.colorbuffer, format::rgba16f, clear_or_load, gc_msaa_samples > 1);
                        cmd_brp.set_2d_depth_stencil(l_res.depthbuffer, format::depth24un_stencil8u, clear_or_load, gc_msaa_samples > 1);
                        cmd_writer.add_command(cmd_brp);
                    }

                    {
                        cmd::draw cmd_draw;
                        cmd_draw.init(l_res.pso_render, l_res.num_indices, l_res.vertex_buffer, l_res.index_buffer);
                        cmd_draw.add_shader_arg(l_res.current_frame().cb_camdata, 0, l_res.current_frame().shaderview_render_vertex);
                        cmd_draw.add_shader_arg(handle::null_resource, 0, l_res.shaderview_render);
                        cmd_draw.add_shader_arg(handle::null_resource, 0, l_res.shaderview_render_ibl);

                        for (auto inst = start; inst < end; ++inst)
                        {
                            cmd_draw.write_root_constants(static_cast<unsigned>(inst));
                            cmd_writer.add_command(cmd_draw);
                        }
                    }

                    cmd_writer.add_command(cmd::end_render_pass{});


                    render_cmd_lists[i] = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
                },
                phi_test::num_instances, phi_test::num_render_threads);


            auto modeldata_upload_sync = td::submit_batched(
                [run_time, model_data, position_modulos](unsigned start, unsigned end) {
                    phi_test::fill_model_matrix_data(*model_data, run_time, start, end, position_modulos);
                },
                phi_test::num_instances, phi_test::num_render_threads);


            cc::capped_vector<handle::command_list, 3> backbuffer_cmd_lists;
            {
                command_stream_writer cmd_writer(thread_cmd_buffer_mem[0], THREAD_BUFFER_SIZE);

                auto const current_backbuffer = backend.acquireBackbuffer();

                if (!current_backbuffer.is_valid())
                {
                    // The vulkan-only scenario: acquiring failed, and we have to discard the current frame
                    td::wait_for(render_sync);
                    backend.discard(render_cmd_lists);
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
                    cmd::transition_resources cmd_trans;
                    cmd_trans.add(current_backbuffer, resource_state::render_target);
                    cmd_trans.add(l_res.colorbuffer_resolve, resource_state::shader_resource, shader_domain::pixel);
                    cmd_writer.add_command(cmd_trans);
                }

                {
                    cmd::begin_render_pass cmd_brp;
                    cmd_brp.viewport = backend.getBackbufferSize();
                    cmd_brp.add_backbuffer_rt(current_backbuffer);
                    cmd_brp.set_null_depth_stencil();
                    cmd_writer.add_command(cmd_brp);
                }

                {
                    cmd::draw cmd_draw;
                    cmd_draw.init(l_res.pso_blit, 3);
                    cmd_draw.add_shader_arg(handle::null_resource, 0, l_res.shaderview_blit);
                    cmd_writer.add_command(cmd_draw);
                }

                {
                    cmd::end_render_pass cmd_erp;
                    cmd_writer.add_command(cmd_erp);
                }

                backbuffer_cmd_lists.push_back(backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size()));

                // ImGui and transition to present
                ImGui_ImplSDL2_NewFrame(window.getSdlWindow());
                ImGui::NewFrame();

                {
                    ImGui::Begin("PBR Demo");

                    ImGui::SliderFloat3("Position modulos", tg::data_ptr(position_modulos), 1.f, 50.f);
                    ImGui::SliderFloat("Camera Distance", &camera_distance, 1.f, 15.f, "%.3f", 2.f);

                    if (ImGui::Button("Reset modulos"))
                        position_modulos = tg::vec3(9, 6, 9);

                    if (ImGui::Button("Reset runtime"))
                        run_time = 0.f;

                    ImGui::End();
                }


                ImGui::Render();
                backbuffer_cmd_lists.push_back(imgui_implementation.render(ImGui::GetDrawData(), current_backbuffer, true));
            }

            // Data upload
            {
                phi_test::global_data camdata;
                camdata.cam_pos = phi_test::get_cam_pos(run_time, camera_distance);
                camdata.cam_vp = phi_test::get_view_projection_matrix(camdata.cam_pos, window.getWidth(), window.getHeight());
                camdata.runtime = static_cast<float>(run_time);
                std::memcpy(l_res.current_frame().cb_camdata_map, &camdata, sizeof(camdata));

                td::wait_for(modeldata_upload_sync);
                std::memcpy(l_res.current_frame().sb_modeldata_map, model_data, sizeof(phi_test::model_matrix_data));

                backend.flushMappedMemory(l_res.current_frame().sb_modeldata);
                backend.flushMappedMemory(l_res.current_frame().cb_camdata);
            }

            // CPU-sync and submit
            td::wait_for(render_sync);
            backend.submit(render_cmd_lists);
            backend.submit(backbuffer_cmd_lists);

            // present
            backend.present();
        }
    }


    backend.flushGPU();
    backend.free(l_res.mat_albedo);
    backend.free(l_res.mat_normal);
    backend.free(l_res.mat_metallic);
    backend.free(l_res.mat_roughness);

    backend.free(l_res.ibl_lut);
    backend.free(l_res.ibl_specular);
    backend.free(l_res.ibl_irradiance);
    backend.free(l_res.shaderview_render_ibl);

    backend.free(l_res.vertex_buffer);
    backend.free(l_res.index_buffer);
    backend.free(l_res.pso_render);
    backend.free(l_res.colorbuffer);
    backend.free(l_res.depthbuffer);
    backend.free(l_res.pso_blit);
    backend.free(l_res.shaderview_blit);
    backend.free(l_res.shaderview_render);

    for (auto const& pfr : l_res.per_frame_resources)
    {
        backend.free(pfr.cb_camdata);
        backend.free(pfr.sb_modeldata);
        backend.free(pfr.shaderview_render_vertex);
    }

    imgui_implementation.shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    backend.destroy();
    window.destroy();
    inc::da::shutdown();
}
