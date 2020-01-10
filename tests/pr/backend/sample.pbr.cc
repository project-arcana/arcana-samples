#include "sample.hh"

#include <iostream>

#include <clean-core/capped_array.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>

#include <typed-geometry/tg.hh>

#include <arcana-incubator/asset-loading/image_loader.hh>
#include <arcana-incubator/asset-loading/mesh_loader.hh>

#include <phantasm-renderer/backend/assets/vertex_attrib_info.hh>
#include <phantasm-renderer/backend/command_stream.hh>
#include <phantasm-renderer/backend/detail/byte_util.hh>
#include <phantasm-renderer/backend/detail/format_size.hh>
#include <phantasm-renderer/backend/detail/unique_buffer.hh>
#include <phantasm-renderer/backend/device_tentative/timer.hh>
#include <phantasm-renderer/backend/device_tentative/window.hh>
#include <phantasm-renderer/default_config.hh>

#include "mip_generation.hh"
#include "sample_scene.hh"
#include "sample_util.hh"

#include <arcana-incubator/imgui/imgui_impl_pr.hh>
#include <arcana-incubator/imgui/imgui_impl_win32.hh>

namespace
{
constexpr bool gc_enable_ibl = 0;
constexpr bool gc_enable_compute_mips = 0;

constexpr unsigned gc_max_num_backbuffers = 4;

#define msaa_enabled false
constexpr unsigned msaa_samples = msaa_enabled ? 4 : 1;
}

void pr_test::run_pbr_sample(pr::backend::Backend& backend, sample_config const& sample_config, const pr::backend::backend_config& backend_config)
{
    using namespace pr::backend;
    CC_RUNTIME_ASSERT(backend_config.num_backbuffers <= gc_max_num_backbuffers && "increase gc_max_num_backbuffers");

    pr::backend::device::Window window;
    window.initialize(sample_config.window_title);
    backend.initialize(backend_config, window);

    // Imgui init
    inc::ImGuiPhantasmImpl imgui_implementation;
    {
        ImGui::SetCurrentContext(ImGui::CreateContext(nullptr));
        ImGui_ImplWin32_Init(window.getHandle());
        window.setEventCallback(ImGui_ImplWin32_WndProcHandler);

        {
            auto const ps_bin = get_shader_binary("res/pr/liveness_sample/shader/bin/imgui_ps.%s", sample_config.shader_ending);
            auto const vs_bin = get_shader_binary("res/pr/liveness_sample/shader/bin/imgui_vs.%s", sample_config.shader_ending);
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
            pr::backend::handle::resource cb_camdata = pr::backend::handle::null_resource;
            pr::backend::handle::resource sb_modeldata = pr::backend::handle::null_resource;
            std::byte* cb_camdata_map = nullptr;
            std::byte* sb_modeldata_map = nullptr;

            pr::backend::handle::shader_view shaderview_render_vertex = pr::backend::handle::null_shader_view;
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

        // blit PSO + SV
        handle::pipeline_state pso_blit = handle::null_pipeline_state;
        handle::shader_view shaderview_blit = handle::null_shader_view;
    };

    resources_t resources;

    // Texture setup
    //
    {
        // resource loading, creation and preprocessing
        static_assert(true, "clang-format");
        {
            pr_test::texture_creation_resources texgen_resources;
            texgen_resources.initialize(backend, sample_config.shader_ending, sample_config.align_mip_rows);

            resources.mat_albedo = texgen_resources.load_texture(pr_test::sample_albedo_path, format::rgba8un, gc_enable_compute_mips, gc_enable_compute_mips);
            resources.mat_normal = texgen_resources.load_texture(pr_test::sample_normal_path, format::rgba8un, gc_enable_compute_mips, false);
            resources.mat_metallic = texgen_resources.load_texture(pr_test::sample_metallic_path, format::r8un, gc_enable_compute_mips, false);
            resources.mat_roughness = texgen_resources.load_texture(pr_test::sample_roughness_path, format::r8un, gc_enable_compute_mips, false);

            if (gc_enable_ibl)
            {
                resources.ibl_specular = texgen_resources.load_filtered_specular_map("res/pr/liveness_sample/texture/ibl/mono_lake.hdr");
                resources.ibl_irradiance = texgen_resources.create_diffuse_irradiance_map(resources.ibl_specular);
                resources.ibl_lut = texgen_resources.create_brdf_lut(256);
            }

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
                tcmd.add(resources.mat_albedo, resource_state::shader_resource, shader_domain_bits::pixel);
                tcmd.add(resources.mat_normal, resource_state::shader_resource, shader_domain_bits::pixel);
                tcmd.add(resources.mat_metallic, resource_state::shader_resource, shader_domain_bits::pixel);
                tcmd.add(resources.mat_roughness, resource_state::shader_resource, shader_domain_bits::pixel);
                writer.add_command(tcmd);
            }

            if (gc_enable_ibl)
            {
                cmd::transition_resources tcmd;
                tcmd.add(resources.ibl_specular, resource_state::shader_resource, shader_domain_bits::pixel);
                tcmd.add(resources.ibl_irradiance, resource_state::shader_resource, shader_domain_bits::pixel);
                tcmd.add(resources.ibl_lut, resource_state::shader_resource, shader_domain_bits::pixel);
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
            auto const mesh_data = pr_test::sample_mesh_binary ? inc::assets::load_binary_mesh(pr_test::sample_mesh_path)
                                                               : inc::assets::load_obj_mesh(pr_test::sample_mesh_path);

            resources.num_indices = unsigned(mesh_data.indices.size());

            auto const vert_size = mesh_data.get_vertex_size_bytes();
            auto const ind_size = mesh_data.get_index_size_bytes();

            resources.vertex_buffer = backend.createBuffer(vert_size, sizeof(inc::assets::simple_vertex));
            resources.index_buffer = backend.createBuffer(ind_size, sizeof(int));

            {
                cmd::transition_resources tcmd;
                tcmd.add(resources.vertex_buffer, resource_state::copy_dest);
                tcmd.add(resources.index_buffer, resource_state::copy_dest);
                writer.add_command(tcmd);
            }

            upload_buffer = backend.createMappedBuffer(vert_size + ind_size);
            std::byte* const upload_mapped = backend.getMappedMemory(upload_buffer);

            std::memcpy(upload_mapped, mesh_data.vertices.data(), vert_size);
            std::memcpy(upload_mapped + vert_size, mesh_data.indices.data(), ind_size);

            writer.add_command(cmd::copy_buffer{resources.vertex_buffer, 0, upload_buffer, 0, vert_size});
            writer.add_command(cmd::copy_buffer{resources.index_buffer, 0, upload_buffer, vert_size, ind_size});

            {
                cmd::transition_resources tcmd;
                tcmd.add(resources.vertex_buffer, resource_state::vertex_buffer);
                tcmd.add(resources.index_buffer, resource_state::index_buffer);
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
        cc::capped_vector<arg::shader_argument_shape, limits::max_shader_arguments> payload_shape;
        {
            // Argument 0, global CBV + model mat structured buffer
            {
                arg::shader_argument_shape arg_shape;
                arg_shape.has_cb = true;
                arg_shape.num_srvs = 1;
                arg_shape.num_uavs = 0;
                arg_shape.num_samplers = 0;
                payload_shape.push_back(arg_shape);
            }

            // Argument 1, pixel shader SRVs
            {
                arg::shader_argument_shape arg_shape;
                arg_shape.has_cb = false;
                arg_shape.num_srvs = 4;
                arg_shape.num_uavs = 0;
                arg_shape.num_samplers = 1;
                payload_shape.push_back(arg_shape);
            }

            // Argument 2, IBL SRVs and LUT sampler
            if (gc_enable_ibl)
            {
                arg::shader_argument_shape arg_shape;
                arg_shape.has_cb = false;
                arg_shape.num_srvs = 3;
                arg_shape.num_uavs = 0;
                arg_shape.num_samplers = 1;
                payload_shape.push_back(arg_shape);
            }
        }

        auto const vertex_binary = get_shader_binary("res/pr/liveness_sample/shader/bin/vertex.%s", sample_config.shader_ending);
        auto const pixel_binary = gc_enable_ibl ? get_shader_binary("res/pr/liveness_sample/shader/bin/pixel.%s", sample_config.shader_ending)
                                                : get_shader_binary("res/pr/liveness_sample/shader/bin/pixel_no_ibl.%s", sample_config.shader_ending);

        CC_RUNTIME_ASSERT(vertex_binary.is_valid() && pixel_binary.is_valid() && "failed to load shaders");

        cc::capped_vector<arg::shader_stage, 6> shader_stages;
        shader_stages.push_back(arg::shader_stage{vertex_binary.get(), vertex_binary.size(), shader_domain::vertex});
        shader_stages.push_back(arg::shader_stage{pixel_binary.get(), pixel_binary.size(), shader_domain::pixel});

        auto const attrib_info = assets::get_vertex_attributes<inc::assets::simple_vertex>();

        arg::framebuffer_config fbconf;
        fbconf.add_render_target(format::rgba16f);
        fbconf.depth_target.push_back(format::depth24un_stencil8u);

        pr::primitive_pipeline_config config;
        config.samples = msaa_samples;

        resources.pso_render = backend.createPipelineState(arg::vertex_format{attrib_info, sizeof(inc::assets::simple_vertex)}, fbconf, payload_shape,
                                                           true, shader_stages, config);
    }

    {
        cc::capped_vector<arg::shader_argument_shape, limits::max_shader_arguments> payload_shape;
        {
            // Argument 0, global CBV + blit target SRV + blit sampler
            {
                arg::shader_argument_shape arg_shape;
                arg_shape.has_cb = false;
                arg_shape.num_srvs = 1;
                arg_shape.num_uavs = 0;
                arg_shape.num_samplers = 1;
                payload_shape.push_back(arg_shape);
            }
        }

        auto const vertex_binary = get_shader_binary("res/pr/liveness_sample/shader/bin/blit_vertex.%s", sample_config.shader_ending);
        auto const pixel_binary = get_shader_binary("res/pr/liveness_sample/shader/bin/blit_pixel.%s", sample_config.shader_ending);

        CC_RUNTIME_ASSERT(vertex_binary.is_valid() && pixel_binary.is_valid() && "failed to load shaders");

        cc::capped_vector<arg::shader_stage, 6> shader_stages;
        shader_stages.push_back(arg::shader_stage{vertex_binary.get(), vertex_binary.size(), shader_domain::vertex});
        shader_stages.push_back(arg::shader_stage{pixel_binary.get(), pixel_binary.size(), shader_domain::pixel});

        arg::framebuffer_config fbconf;
        fbconf.add_render_target(backend.getBackbufferFormat());

        pr::primitive_pipeline_config config;
        config.cull = pr::cull_mode::front;

        resources.pso_blit = backend.createPipelineState(arg::vertex_format{{}, 0}, fbconf, payload_shape, false, shader_stages, config);
    }

    {
        resources.per_frame_resources.emplace(backend_config.num_backbuffers);

        shader_view_element srv;
        srv.init_as_structured_buffer(handle::null_resource, pr_test::num_instances, sizeof(tg::mat4));

        for (auto& pfb : resources.per_frame_resources)
        {
            pfb.cb_camdata = backend.createMappedBuffer(sizeof(pr_test::global_data));
            pfb.cb_camdata_map = backend.getMappedMemory(pfb.cb_camdata);

            pfb.sb_modeldata = backend.createMappedBuffer(sizeof(pr_test::model_matrix_data));
            pfb.sb_modeldata_map = backend.getMappedMemory(pfb.sb_modeldata);

            srv.resource = pfb.sb_modeldata;
            pfb.shaderview_render_vertex = backend.createShaderView(cc::span{srv}, {}, {});
        }
    }

    {
        sampler_config mat_sampler;
        mat_sampler.init_default(sampler_filter::anisotropic);

        cc::capped_vector<shader_view_element, 4> srv_elems;
        srv_elems.emplace_back().init_as_tex2d(resources.mat_albedo, format::rgba8un);
        srv_elems.emplace_back().init_as_tex2d(resources.mat_normal, format::rgba8un);
        srv_elems.emplace_back().init_as_tex2d(resources.mat_metallic, format::r8un);
        srv_elems.emplace_back().init_as_tex2d(resources.mat_roughness, format::r8un);
        resources.shaderview_render = backend.createShaderView(srv_elems, {}, cc::span{mat_sampler});
    }

    if (gc_enable_ibl)
    {
        sampler_config lut_sampler;
        lut_sampler.init_default(sampler_filter::min_mag_mip_linear, 1);
        lut_sampler.address_u = sampler_address_mode::clamp;
        lut_sampler.address_v = sampler_address_mode::clamp;

        cc::capped_vector<shader_view_element, 3> srv_elems;
        srv_elems.emplace_back().init_as_texcube(resources.ibl_specular, format::rgba16f);
        srv_elems.emplace_back().init_as_texcube(resources.ibl_irradiance, format::rgba16f);
        srv_elems.emplace_back().init_as_tex2d(resources.ibl_lut, format::rg16f);

        resources.shaderview_render_ibl = backend.createShaderView(srv_elems, {}, cc::span{lut_sampler});
    }

    resources.depthbuffer = backend.createRenderTarget(format::depth24un_stencil8u, 150, 150, msaa_samples);
    resources.colorbuffer = backend.createRenderTarget(format::rgba16f, 150, 150, msaa_samples);

    auto const on_resize_func = [&]() {
        backend.flushGPU();
        auto const backbuffer_size = backend.getBackbufferSize();
        auto const w = static_cast<unsigned>(backbuffer_size.width);
        auto const h = static_cast<unsigned>(backbuffer_size.height);
        std::cout << "backbuffer resize to " << w << "x" << h << std::endl;

        backend.free(resources.depthbuffer);
        resources.depthbuffer = backend.createRenderTarget(format::depth24un_stencil8u, w, h, msaa_samples);
        backend.free(resources.colorbuffer);
        resources.colorbuffer = backend.createRenderTarget(format::rgba16f, w, h, msaa_samples);

        {
            if (resources.shaderview_blit.is_valid())
                backend.free(resources.shaderview_blit);

            sampler_config rt_sampler;
            rt_sampler.init_default(sampler_filter::min_mag_mip_point);

            cc::capped_vector<shader_view_element, 1> srv_elems;
            srv_elems.emplace_back().init_as_tex2d(resources.colorbuffer, format::rgba16f, msaa_enabled);
            resources.shaderview_blit = backend.createShaderView(srv_elems, {}, cc::span{rt_sampler});
        }

        {
            std::byte writer_mem[sizeof(cmd::transition_resources)];
            command_stream_writer writer(writer_mem, sizeof(writer_mem));

            cmd::transition_resources transition_cmd;
            transition_cmd.add(resources.depthbuffer, resource_state::depth_write);
            transition_cmd.add(resources.colorbuffer, resource_state::render_target);
            writer.add_command(transition_cmd);

            auto const cl = backend.recordCommandList(writer.buffer(), writer.size());
            backend.submit(cc::span{cl});
        }
    };

    // Main loop
    device::Timer timer;
    float run_time = 0.f;
    unsigned framecounter = 450;


// cacheline-sized tasks call for desperate measures (macro)
#define THREAD_BUFFER_SIZE (static_cast<size_t>((sizeof(cmd::draw) * (pr_test::num_instances / pr_test::num_render_threads)) + 1024u))

    cc::array<std::byte*, pr_test::num_render_threads + 1> thread_cmd_buffer_mem;

    for (auto& mem : thread_cmd_buffer_mem)
        mem = static_cast<std::byte*>(std::malloc(THREAD_BUFFER_SIZE));

    CC_DEFER
    {
        for (std::byte* mem : thread_cmd_buffer_mem)
            std::free(mem);
    };

    pr_test::model_matrix_data* model_data = new pr_test::model_matrix_data();
    CC_DEFER { delete model_data; };

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

            ++resources.current_frame_index;
            if (resources.current_frame_index >= backend_config.num_backbuffers)
                resources.current_frame_index -= backend_config.num_backbuffers;

            if (backend.clearPendingResize())
                on_resize_func();

            cc::array<handle::command_list, pr_test::num_render_threads> render_cmd_lists;
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
                        cmd_trans.add(resources.colorbuffer, resource_state::render_target);
                        cmd_writer.add_command(cmd_trans);
                    }
                    {
                        cmd::begin_render_pass cmd_brp;
                        cmd_brp.viewport = backend.getBackbufferSize();
                        cmd_brp.add_2d_rt(resources.colorbuffer, format::rgba16f, clear_or_load, msaa_enabled);
                        cmd_brp.set_2d_depth_stencil(resources.depthbuffer, format::depth24un_stencil8u, clear_or_load, msaa_enabled);
                        cmd_writer.add_command(cmd_brp);
                    }

                    {
                        cmd::draw cmd_draw;
                        cmd_draw.init(resources.pso_render, resources.num_indices, resources.vertex_buffer, resources.index_buffer);
                        cmd_draw.add_shader_arg(resources.current_frame().cb_camdata, 0, resources.current_frame().shaderview_render_vertex);
                        cmd_draw.add_shader_arg(handle::null_resource, 0, resources.shaderview_render);

                        if constexpr (gc_enable_ibl)
                            cmd_draw.add_shader_arg(handle::null_resource, 0, resources.shaderview_render_ibl);

                        for (auto inst = start; inst < end; ++inst)
                        {
                            cmd_draw.write_root_constants(static_cast<unsigned>(inst));
                            cmd_writer.add_command(cmd_draw);
                        }
                    }

                    cmd_writer.add_command(cmd::end_render_pass{});


                    render_cmd_lists[i] = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
                },
                pr_test::num_instances, pr_test::num_render_threads);

            auto modeldata_upload_sync = td::submit_batched(
                [run_time, model_data](unsigned start, unsigned end) { pr_test::fill_model_matrix_data(*model_data, run_time, start, end); },
                pr_test::num_instances, pr_test::num_render_threads);


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

                {
                    cmd::transition_resources cmd_trans;
                    cmd_trans.add(current_backbuffer, resource_state::render_target);
                    cmd_trans.add(resources.colorbuffer, resource_state::shader_resource, shader_domain_bits::pixel);
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
                    cmd_draw.init(resources.pso_blit, 3);
                    cmd_draw.add_shader_arg(handle::null_resource, 0, resources.shaderview_blit);
                    cmd_writer.add_command(cmd_draw);
                }

                {
                    cmd::end_render_pass cmd_erp;
                    cmd_writer.add_command(cmd_erp);
                }

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(current_backbuffer, resource_state::present);
                    // cmd_writer.add_command(tcmd);
                }

                backbuffer_cmd_lists.push_back(backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size()));

                // ImGui and transition to present

                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                ImGui::ShowDemoWindow(nullptr);
                ImGui::Render();
                backbuffer_cmd_lists.push_back(imgui_implementation.render(ImGui::GetDrawData(), current_backbuffer, true));
            }

            // Data upload
            {
                pr_test::global_data camdata;
                camdata.cam_pos = pr_test::get_cam_pos(run_time);
                camdata.cam_vp = pr_test::get_view_projection_matrix(camdata.cam_pos, window.getWidth(), window.getHeight());
                camdata.runtime = static_cast<float>(run_time);
                std::memcpy(resources.current_frame().cb_camdata_map, &camdata, sizeof(camdata));

                td::wait_for(modeldata_upload_sync);
                std::memcpy(resources.current_frame().sb_modeldata_map, model_data, sizeof(pr_test::model_matrix_data));

                backend.flushMappedMemory(resources.current_frame().sb_modeldata);
                backend.flushMappedMemory(resources.current_frame().cb_camdata);
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
    backend.free(resources.mat_albedo);
    backend.free(resources.mat_normal);
    backend.free(resources.mat_metallic);
    backend.free(resources.mat_roughness);

    if (gc_enable_ibl)
    {
        backend.free(resources.ibl_lut);
        backend.free(resources.ibl_specular);
        backend.free(resources.ibl_irradiance);
        backend.free(resources.shaderview_render_ibl);
    }

    backend.free(resources.vertex_buffer);
    backend.free(resources.index_buffer);
    backend.free(resources.pso_render);
    backend.free(resources.colorbuffer);
    backend.free(resources.depthbuffer);
    backend.free(resources.pso_blit);
    backend.free(resources.shaderview_blit);
    backend.free(resources.shaderview_render);

    for (auto const& pfr : resources.per_frame_resources)
    {
        backend.free(pfr.cb_camdata);
        backend.free(pfr.sb_modeldata);
        backend.free(pfr.shaderview_render_vertex);
    }

    imgui_implementation.shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
