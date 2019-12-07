#include "backend_common.hh"

#include <phantasm-renderer/backend/detail/byte_util.hh>
#include <phantasm-renderer/backend/detail/format_size.hh>
#include <phantasm-renderer/backend/detail/unique_buffer.hh>

void pr_test::copy_mipmaps_to_texture(pr::backend::command_stream_writer& writer,
                                      pr::backend::handle::resource upload_buffer,
                                      std::byte* upload_buffer_map,
                                      pr::backend::handle::resource dest_texture,
                                      pr::backend::format format,
                                      const pr::backend::assets::image_size& img_size,
                                      const pr::backend::assets::image_data& img_data,
                                      bool use_d3d12_per_row_alingment)
{
    using namespace pr::backend;
    auto const bytes_per_pixel = detail::pr_format_size_bytes(format);

    CC_RUNTIME_ASSERT(img_size.array_size == 1 && "array upload unimplemented");
    cmd::copy_buffer_to_texture command;
    command.source = upload_buffer;
    command.destination = dest_texture;
    command.texture_format = format;
    command.dest_mip_size = img_size.num_mipmaps;

    auto accumulated_offset = 0u;

    for (auto a = 0u; a < img_size.array_size; ++a)
    {
        command.dest_array_index = a;

        // copy all the mip slices into the offsets specified by the footprint structure
        //
        for (auto mip = 0u; mip < img_size.num_mipmaps; ++mip)
        {
            auto const mip_width = cc::max(unsigned(tg::floor(img_size.width / tg::pow(2.f, float(mip)))), 1u);
            auto const mip_height = cc::max(unsigned(tg::floor(img_size.height / tg::pow(2.f, float(mip)))), 1u);

            command.dest_width = mip_width;
            command.dest_height = mip_height;
            command.source_offset = accumulated_offset;
            command.dest_mip_index = mip;

            writer.add_command(command);

            // MIP maps are 256-byte aligned per row
            auto const row_pitch = use_d3d12_per_row_alingment ? mem::align_up(bytes_per_pixel * command.dest_width, 256) : bytes_per_pixel * command.dest_width;
            auto const custom_offset = row_pitch * command.dest_height;
            accumulated_offset += custom_offset;

            pr::backend::assets::copy_subdata(img_data, upload_buffer_map + command.source_offset, row_pitch, command.dest_width * bytes_per_pixel,
                                              command.dest_height);
        }
    }
}

unsigned pr_test::get_mipmap_upload_size(pr::backend::format format, const pr::backend::assets::image_size& img_size)
{
    using namespace pr::backend;
    auto const bytes_per_pixel = detail::pr_format_size_bytes(format);
    auto res_bytes = 0u;

    for (auto a = 0u; a < img_size.array_size; ++a)
    {
        for (auto mip = 0u; mip < img_size.num_mipmaps; ++mip)
        {
            auto const mip_width = cc::max(unsigned(tg::floor(img_size.width / tg::pow(2.f, float(mip)))), 1u);
            auto const mip_height = cc::max(unsigned(tg::floor(img_size.height / tg::pow(2.f, float(mip)))), 1u);

            auto const row_pitch = mem::align_up(bytes_per_pixel * mip_width, 256);
            auto const custom_offset = row_pitch * mip_height;
            res_bytes += custom_offset;
        }
    }

    //    uint32_t num_rows[D3D12_REQ_MIP_LEVELS] = {0};
    //    UINT64 row_sizes_in_bytes[D3D12_REQ_MIP_LEVELS] = {0};
    //    D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed_subres_tex2d[D3D12_REQ_MIP_LEVELS];
    //    size_t res;
    //    {
    //        auto const res_desc = CD3DX12_RESOURCE_DESC::Tex2D(util::to_dxgi_format(format), img_size.width, img_size.height, 1, UINT16(img_size.num_mipmaps));
    //        device.GetCopyableFootprints(&res_desc, 0, img_size.num_mipmaps, 0, placed_subres_tex2d, num_rows, row_sizes_in_bytes, &res);
    //    }

    //    std::cout << "Custom: " << res_bytes << ", D3D12: " << res << std::endl;

    return res_bytes;
}

void pr_test::run_sample(pr::backend::Backend& backend, const pr::backend::backend_config& config, sample_config const& sample_config)
{
    using namespace pr::backend;

    pr::backend::device::Window window;
    window.initialize(sample_config.window_title);
    backend.initialize(config, window);

    {
        struct resources_t
        {
            handle::resource material = handle::null_resource;
            handle::resource vertex_buffer = handle::null_resource;
            handle::resource index_buffer = handle::null_resource;
            unsigned num_indices = 0;

            handle::resource cb_camdata = handle::null_resource;
            handle::resource cb_modeldata = handle::null_resource;

            handle::pipeline_state pso_render = handle::null_pipeline_state;
            handle::shader_view shaderview_render = handle::null_shader_view;
            handle::resource depthbuffer = handle::null_resource;
            handle::resource colorbuffer = handle::null_resource;

            handle::pipeline_state pso_blit = handle::null_pipeline_state;
            handle::shader_view shaderview_blit = handle::null_shader_view;
        };

        resources_t resources;

        // Resource setup
        //
        {
            handle::resource ng_upbuffer_material;
            handle::resource ng_upbuff;
            CC_DEFER
            {
                backend.free(ng_upbuffer_material);
                backend.free(ng_upbuff);
            };

            auto const buffer_size = 1024ull * 16;
            auto* const buffer = static_cast<std::byte*>(std::malloc(buffer_size));
            CC_DEFER { std::free(buffer); };

            command_stream_writer writer(buffer, buffer_size);


            {
                assets::image_size img_size;
                auto const img_data = assets::load_image(pr_test::sample_texture_path, img_size);
                CC_RUNTIME_ASSERT(assets::is_valid(img_data) && "failed to load texture");
                CC_DEFER { assets::free(img_data); };

                resources.material = backend.createTexture2D(format::rgba8un, img_size.width, img_size.height, img_size.num_mipmaps);

                ng_upbuffer_material = backend.createMappedBuffer(pr_test::get_mipmap_upload_size(format::rgba8un, img_size));


                {
                    cmd::transition_resources transition_cmd;
                    transition_cmd.transitions.push_back(cmd::transition_resources::transition_info{resources.material, resource_state::copy_dest});
                    writer.add_command(transition_cmd);
                }

                pr_test::copy_mipmaps_to_texture(writer, ng_upbuffer_material, backend.getMappedMemory(ng_upbuffer_material), resources.material,
                                                 format::rgba8un, img_size, img_data, sample_config.align_mip_rows);
            }

            // create vertex and index buffer
            {
                auto const mesh_data = assets::load_obj_mesh(pr_test::sample_mesh_path);
                resources.num_indices = unsigned(mesh_data.indices.size());

                auto const vert_size = mesh_data.get_vertex_size_bytes();
                auto const ind_size = mesh_data.get_index_size_bytes();

                resources.vertex_buffer = backend.createBuffer(vert_size, resource_state::copy_dest, sizeof(assets::simple_vertex));
                resources.index_buffer = backend.createBuffer(ind_size, resource_state::copy_dest, sizeof(int));


                {
                    cmd::transition_resources transition_cmd;
                    transition_cmd.transitions.push_back(cmd::transition_resources::transition_info{resources.vertex_buffer, resource_state::copy_dest});
                    transition_cmd.transitions.push_back(cmd::transition_resources::transition_info{resources.index_buffer, resource_state::copy_dest});
                    writer.add_command(transition_cmd);
                }

                ng_upbuff = backend.createMappedBuffer(vert_size + ind_size);
                std::byte* const upload_mapped = backend.getMappedMemory(ng_upbuff);

                std::memcpy(upload_mapped, mesh_data.vertices.data(), vert_size);
                std::memcpy(upload_mapped + vert_size, mesh_data.indices.data(), ind_size);

                writer.add_command(cmd::copy_buffer{resources.vertex_buffer, 0, ng_upbuff, 0, vert_size});
                writer.add_command(cmd::copy_buffer{resources.index_buffer, 0, ng_upbuff, vert_size, ind_size});
            }

            // transition resources, record and free the upload command list, and upload buffers
            {
                cmd::transition_resources transition_cmd;
                transition_cmd.transitions.push_back(cmd::transition_resources::transition_info{resources.vertex_buffer, resource_state::vertex_buffer});
                transition_cmd.transitions.push_back(cmd::transition_resources::transition_info{resources.index_buffer, resource_state::index_buffer});
                transition_cmd.transitions.push_back(cmd::transition_resources::transition_info{resources.material, resource_state::shader_resource});
                writer.add_command(transition_cmd);

                auto const copy_cmd_list = backend.recordCommandList(writer.buffer(), writer.size());
                backend.submit(cc::span{copy_cmd_list});

                backend.flushGPU();
            }
        }

        {
            cc::capped_vector<arg::shader_argument_shape, 4> payload_shape;
            {
                // Argument 0, camera CBV
                {
                    arg::shader_argument_shape arg_shape;
                    arg_shape.has_cb = true;
                    arg_shape.num_srvs = 0;
                    arg_shape.num_uavs = 0;
                    payload_shape.push_back(arg_shape);
                }

                // Argument 1, pixel shader SRV and model matrix CBV
                {
                    arg::shader_argument_shape arg_shape;
                    arg_shape.has_cb = true;
                    arg_shape.num_srvs = 1;
                    arg_shape.num_uavs = 0;
                    payload_shape.push_back(arg_shape);
                }
            }

            auto const vertex_binary = pr::backend::detail::unique_buffer::create_from_binary_file(sample_config.path_render_vs);
            auto const pixel_binary = pr::backend::detail::unique_buffer::create_from_binary_file(sample_config.path_render_ps);

            CC_RUNTIME_ASSERT(vertex_binary.is_valid() && pixel_binary.is_valid() && "failed to load shaders");

            cc::capped_vector<arg::shader_stage, 6> shader_stages;
            shader_stages.push_back(arg::shader_stage{vertex_binary.get(), vertex_binary.size(), shader_domain::vertex});
            shader_stages.push_back(arg::shader_stage{pixel_binary.get(), pixel_binary.size(), shader_domain::pixel});

            auto const attrib_info = assets::get_vertex_attributes<assets::simple_vertex>();
            cc::capped_vector<format, 8> rtv_formats;
            rtv_formats.push_back(format::rgba16f);
            format const dsv_format = format::depth24un_stencil8u;

            sampler_config mat_sampler;
            mat_sampler.init_default(sampler_filter::min_mag_mip_linear);
            // mat_sampler.min_lod = 8.f;

            resources.pso_render = backend.createPipelineState(arg::vertex_format{attrib_info, sizeof(assets::simple_vertex)},
                                                               arg::framebuffer_format{rtv_formats, cc::span{dsv_format}}, payload_shape,
                                                               cc::span{mat_sampler}, shader_stages, pr::default_config);
        }

        {
            cc::capped_vector<arg::shader_argument_shape, 4> payload_shape;
            {
                // Argument 0, blit target SRV
                {
                    arg::shader_argument_shape arg_shape;
                    arg_shape.has_cb = false;
                    arg_shape.num_srvs = 1;
                    arg_shape.num_uavs = 0;
                    payload_shape.push_back(arg_shape);
                }
            }

            auto const vertex_binary = pr::backend::detail::unique_buffer::create_from_binary_file(sample_config.path_blit_vs);
            auto const pixel_binary = pr::backend::detail::unique_buffer::create_from_binary_file(sample_config.path_blit_ps);

            CC_RUNTIME_ASSERT(vertex_binary.is_valid() && pixel_binary.is_valid() && "failed to load shaders");

            cc::capped_vector<arg::shader_stage, 6> shader_stages;
            shader_stages.push_back(arg::shader_stage{vertex_binary.get(), vertex_binary.size(), shader_domain::vertex});
            shader_stages.push_back(arg::shader_stage{pixel_binary.get(), pixel_binary.size(), shader_domain::pixel});

            cc::capped_vector<format, 8> rtv_formats;
            rtv_formats.push_back(backend.getBackbufferFormat());

            pr::primitive_pipeline_config config;
            config.cull = pr::cull_mode::front;


            sampler_config rt_sampler;
            rt_sampler.init_default(sampler_filter::min_mag_mip_point);

            resources.pso_blit = backend.createPipelineState(arg::vertex_format{{}, 0}, arg::framebuffer_format{rtv_formats, {}}, payload_shape,
                                                             cc::span{rt_sampler}, shader_stages, config);
        }

        {
            cc::capped_vector<shader_view_element, 2> srv_elems;
            srv_elems.emplace_back().init_as_tex2d(resources.material, format::rgba8un);
            resources.shaderview_render = backend.createShaderView(srv_elems);
        }
        resources.cb_camdata = backend.createMappedBuffer(sizeof(tg::mat4));
        std::byte* const cb_camdata_map = backend.getMappedMemory(resources.cb_camdata);

        resources.cb_modeldata = backend.createMappedBuffer(sizeof(pr_test::model_matrix_data));
        std::byte* const cb_modeldata_map = backend.getMappedMemory(resources.cb_modeldata);

        resources.depthbuffer = backend.createRenderTarget(format::depth24un_stencil8u, 150, 150);
        resources.colorbuffer = backend.createRenderTarget(format::rgba16f, 150, 150);

        auto const on_resize_func = [&](int w, int h) {
            std::cout << "resize to " << w << "x" << h << std::endl;

            backend.flushGPU();

            backend.free(resources.depthbuffer);
            resources.depthbuffer = backend.createRenderTarget(format::depth24un_stencil8u, w, h);
            backend.free(resources.colorbuffer);
            resources.colorbuffer = backend.createRenderTarget(format::rgba16f, w, h);

            {
                if (resources.shaderview_blit.is_valid())
                    backend.free(resources.shaderview_blit);

                cc::capped_vector<shader_view_element, 1> srv_elems;
                srv_elems.emplace_back().init_as_tex2d(resources.colorbuffer, format::rgba16f);
                resources.shaderview_blit = backend.createShaderView(srv_elems);
            }

            {
                std::byte writer_mem[sizeof(cmd::transition_resources)];
                command_stream_writer writer(writer_mem, sizeof(writer_mem));

                cmd::transition_resources transition_cmd;
                transition_cmd.transitions.push_back(cmd::transition_resources::transition_info{resources.depthbuffer, resource_state::depth_write});
                transition_cmd.transitions.push_back(cmd::transition_resources::transition_info{resources.colorbuffer, resource_state::render_target});
                writer.add_command(transition_cmd);

                auto const cl = backend.recordCommandList(writer.buffer(), writer.size());
                backend.submit(cc::span{cl});
            }

            backend.resize(w, h);
        };

        // Main loop
        device::Timer timer;
        double run_time = 0.0;


        // cacheline-sized tasks call for desperate measures (macro)
#define THREAD_BUFFER_SIZE (static_cast<size_t>(1024ull * 100))

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

        auto framecounter = 0;
        while (!window.isRequestingClose())
        {
            window.pollEvents();
            if (window.isPendingResize())
            {
                if (!window.isMinimized())
                    on_resize_func(window.getWidth(), window.getHeight());
                window.clearPendingResize();
            }

            if (!window.isMinimized())
            {
                auto const frametime = timer.elapsedMillisecondsD();
                timer.restart();
                run_time += frametime / 1000.;

                {
                    ++framecounter;
                    if (framecounter == 480)
                    {
                        std::cout << "Frametime: " << frametime << "ms" << std::endl;
                        framecounter = 0;
                    }
                }

                // Data upload
                {
                    auto const vp = pr_test::get_view_projection_matrix(run_time, window.getWidth(), window.getHeight());
                    std::memcpy(cb_camdata_map, &vp, sizeof(vp));

                    model_data->fill(run_time);
                    std::memcpy(cb_modeldata_map, model_data, sizeof(pr_test::model_matrix_data));
                }

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
                            cmd_trans.transitions.push_back(cmd::transition_resources::transition_info{resources.colorbuffer, resource_state::render_target});
                            cmd_writer.add_command(cmd_trans);
                        }

                        cmd::begin_render_pass cmd_brp;
                        cmd_brp.viewport = backend.getBackbufferSize();

                        cmd_brp.render_targets.push_back(cmd::begin_render_pass::render_target_info{{}, {0.f, 0.f, 0.f, 1.f}, clear_or_load});
                        cmd_brp.render_targets.back().sve.init_as_tex2d(resources.colorbuffer, format::rgba16f);

                        cmd_brp.depth_target = cmd::begin_render_pass::depth_stencil_info{{}, 1.f, 0, clear_or_load};
                        cmd_brp.depth_target.sve.init_as_tex2d(resources.depthbuffer, format::depth24un_stencil8u);

                        cmd_writer.add_command(cmd_brp);


                        cmd::draw cmd_draw;
                        cmd_draw.num_indices = resources.num_indices;
                        cmd_draw.index_buffer = resources.index_buffer;
                        cmd_draw.vertex_buffer = resources.vertex_buffer;
                        cmd_draw.pipeline_state = resources.pso_render;
                        cmd_draw.shader_arguments.push_back(shader_argument{resources.cb_camdata, 0, handle::null_shader_view});
                        cmd_draw.shader_arguments.push_back(shader_argument{resources.cb_modeldata, 0, resources.shaderview_render});

                        for (auto inst = start; inst < end; ++inst)
                        {
                            cmd_draw.shader_arguments[1].constant_buffer_offset = sizeof(pr_test::model_matrix_data::padded_instance) * inst;
                            cmd_writer.add_command(cmd_draw);
                        }

                        cmd_writer.add_command(cmd::end_render_pass{});


                        render_cmd_lists[i] = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
                    },
                    pr_test::model_matrix_data::num_instances, pr_test::num_render_threads);


                {
                    command_stream_writer cmd_writer(thread_cmd_buffer_mem[0], THREAD_BUFFER_SIZE);

                    auto const ng_backbuffer = backend.acquireBackbuffer();

                    if (!ng_backbuffer.is_valid())
                    {
                        // The vulkan-only scenario: acquiring failed, and we have to discard the current frame

                        td::wait_for(render_sync);
                        for (auto const cl : render_cmd_lists)
                        {
                            if (cl.is_valid())
                                backend.discard(cl);
                        }
                        continue;
                    }

                    {
                        cmd::transition_resources cmd_trans;
                        cmd_trans.transitions.push_back(cmd::transition_resources::transition_info{ng_backbuffer, resource_state::render_target});
                        cmd_trans.transitions.push_back(cmd::transition_resources::transition_info{resources.colorbuffer, resource_state::shader_resource});
                        cmd_writer.add_command(cmd_trans);
                    }

                    {
                        cmd::begin_render_pass cmd_brp;
                        cmd_brp.viewport = backend.getBackbufferSize();
                        cmd_brp.render_targets.push_back(cmd::begin_render_pass::render_target_info{{}, {0.f, 0.f, 0.f, 1.f}, rt_clear_type::clear});
                        cmd_brp.render_targets.back().sve.init_as_backbuffer(ng_backbuffer);
                        cmd_brp.depth_target.sve.init_as_null();
                        cmd_writer.add_command(cmd_brp);
                    }

                    {
                        cmd::draw cmd_draw;
                        cmd_draw.num_indices = 3;
                        cmd_draw.index_buffer = handle::null_resource;
                        cmd_draw.vertex_buffer = handle::null_resource;
                        cmd_draw.pipeline_state = resources.pso_blit;
                        cmd_draw.shader_arguments.push_back(shader_argument{handle::null_resource, 0, resources.shaderview_blit});
                        cmd_writer.add_command(cmd_draw);
                    }

                    {
                        cmd::end_render_pass cmd_erp;
                        cmd_writer.add_command(cmd_erp);
                    }

                    {
                        cmd::transition_resources cmd_trans;
                        cmd_trans.transitions.push_back(cmd::transition_resources::transition_info{ng_backbuffer, resource_state::present});
                        cmd_writer.add_command(cmd_trans);
                    }
                    auto const present_list = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());

                    td::wait_for(render_sync);
                    backend.submit(render_cmd_lists);
                    backend.submit(cc::span{present_list});
                }


                backend.present();
            }
        }


        backend.flushGPU();
        backend.free(resources.material);
        backend.free(resources.vertex_buffer);
        backend.free(resources.index_buffer);
        backend.free(resources.pso_render);
        backend.free(resources.cb_camdata);
        backend.free(resources.cb_modeldata);
        backend.free(resources.colorbuffer);
        backend.free(resources.depthbuffer);
        backend.free(resources.pso_blit);
    }
}
