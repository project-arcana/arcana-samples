#ifdef PR_BACKEND_D3D12
#include <nexus/test.hh>

#include "backend_common.hh"

#include <phantasm-renderer/backend/d3d12/BackendD3D12.hh>
#include <phantasm-renderer/backend/d3d12/adapter_choice_util.hh>
#include <phantasm-renderer/backend/d3d12/common/d3dx12.hh>
#include <phantasm-renderer/backend/d3d12/common/dxgi_format.hh>
#include <phantasm-renderer/backend/d3d12/pipeline_state.hh>
#include <phantasm-renderer/backend/d3d12/pools/cmd_list_pool.hh>
#include <phantasm-renderer/backend/d3d12/root_signature.hh>

namespace
{
void copy_mipmaps_to_texture(ID3D12Device& device,
                             pr::backend::command_stream_writer& writer,
                             pr::backend::handle::resource upload_buffer,
                             std::byte* upload_buffer_map,
                             pr::backend::handle::resource dest_texture,
                             pr::backend::format format,
                             pr::backend::assets::image_size const& img_size,
                             pr::backend::assets::image_data const& img_data)
{
    using namespace pr::backend;
    using namespace pr::backend::d3d12;

    auto const native_format = util::to_dxgi_format(format);

    // Get mip footprints (if it is an array we reuse the mip footprints for all the elements of the array)
    //
    uint32_t num_rows[D3D12_REQ_MIP_LEVELS] = {0};
    UINT64 row_sizes_in_bytes[D3D12_REQ_MIP_LEVELS] = {0};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed_subres_tex2d[D3D12_REQ_MIP_LEVELS];
    UINT64 upload_heap_size;
    {
        auto const res_desc = CD3DX12_RESOURCE_DESC::Tex2D(native_format, img_size.width, img_size.height, 1, UINT16(img_size.num_mipmaps));
        device.GetCopyableFootprints(&res_desc, 0, img_size.num_mipmaps, 0, placed_subres_tex2d, num_rows, row_sizes_in_bytes, &upload_heap_size);
    }

    auto const bytes_per_pixel = util::get_dxgi_bytes_per_pixel(native_format);

    CC_RUNTIME_ASSERT(img_size.array_size == 1 && "array upload unimplemented");
    cmd::copy_buffer_to_texture command;
    command.source = upload_buffer;
    command.destination = dest_texture;
    command.texture_format = format;

    for (auto a = 0u; a < img_size.array_size; ++a)
    {
        // copy all the mip slices into the offsets specified by the footprint structure
        //
        for (auto mip = 0u; mip < img_size.num_mipmaps; ++mip)
        {
            command.mip_width = placed_subres_tex2d[mip].Footprint.Width;
            command.mip_height = placed_subres_tex2d[mip].Footprint.Height;
            command.row_pitch = placed_subres_tex2d[mip].Footprint.RowPitch;
            command.source_offset = placed_subres_tex2d[mip].Offset;
            command.subresource_index = a * img_size.num_mipmaps + mip;

            writer.add_command(command);

            pr::backend::assets::copy_subdata(img_data, upload_buffer_map + command.source_offset, command.row_pitch,
                                              command.mip_width * bytes_per_pixel, command.mip_height);
        }
    }
}

size_t get_mipmap_upload_size(ID3D12Device& device, pr::backend::format format, pr::backend::assets::image_size const& img_size)
{
    using namespace pr::backend;
    using namespace pr::backend::d3d12;

    uint32_t num_rows[D3D12_REQ_MIP_LEVELS] = {0};
    UINT64 row_sizes_in_bytes[D3D12_REQ_MIP_LEVELS] = {0};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed_subres_tex2d[D3D12_REQ_MIP_LEVELS];
    size_t res;
    {
        auto const res_desc = CD3DX12_RESOURCE_DESC::Tex2D(util::to_dxgi_format(format), img_size.width, img_size.height, 1, UINT16(img_size.num_mipmaps));
        device.GetCopyableFootprints(&res_desc, 0, img_size.num_mipmaps, 0, placed_subres_tex2d, num_rows, row_sizes_in_bytes, &res);
    }

    return res;
}

void run_d3d12_sample(pr::backend::backend_config const& config)
{
    using namespace pr::backend;
    using namespace pr::backend::d3d12;

    pr::backend::device::Window window;
    window.initialize("pr::backend::d3d12 liveness");

    BackendD3D12 backend;
    backend.initialize(config, window);

    {
        struct resources_t
        {
            handle::resource material;
            handle::resource vertex_buffer;
            handle::resource index_buffer;
            unsigned num_indices = 0;

            handle::resource cb_camdata;
            handle::resource cb_modeldata;

            handle::pipeline_state pso_render;
            handle::shader_view shaderview_render;
            handle::resource depthbuffer;
            handle::resource colorbuffer;

            handle::pipeline_state pso_blit;
            handle::shader_view shaderview_blit;
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

                ng_upbuffer_material = backend.createMappedBuffer(get_mipmap_upload_size(backend.getDevice(), format::rgba8un, img_size));


                {
                    cmd::transition_resources transition_cmd;
                    transition_cmd.transitions.push_back(cmd::transition_resources::transition_info{resources.material, resource_state::copy_dest});
                    writer.add_command(transition_cmd);
                }

                copy_mipmaps_to_texture(backend.getDevice(), writer, ng_upbuffer_material, backend.getMappedMemory(ng_upbuffer_material),
                                        resources.material, format::rgba8un, img_size, img_data);
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

            auto const vertex_binary = pr::backend::detail::unique_buffer::create_from_binary_file("res/pr/liveness_sample/shader/dxil/vertex.dxil");
            auto const pixel_binary = pr::backend::detail::unique_buffer::create_from_binary_file("res/pr/liveness_sample/shader/dxil/pixel.dxil");

            CC_RUNTIME_ASSERT(vertex_binary.is_valid() && pixel_binary.is_valid() && "failed to load shaders");

            cc::capped_vector<arg::shader_stage, 6> shader_stages;
            shader_stages.push_back(arg::shader_stage{vertex_binary.get(), vertex_binary.size(), shader_domain::vertex});
            shader_stages.push_back(arg::shader_stage{pixel_binary.get(), pixel_binary.size(), shader_domain::pixel});

            auto const attrib_info = assets::get_vertex_attributes<assets::simple_vertex>();
            cc::capped_vector<format, 8> rtv_formats;
            rtv_formats.push_back(format::rgba16f);
            format const dsv_format = format::depth24un_stencil8u;

            resources.pso_render = backend.createPipelineState(arg::vertex_format{attrib_info, sizeof(assets::simple_vertex)},
                                                               arg::framebuffer_format{rtv_formats, cc::span{dsv_format}}, payload_shape,
                                                               shader_stages, pr::default_config);
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

            auto const vertex_binary
                = pr::backend::detail::unique_buffer::create_from_binary_file("res/pr/liveness_sample/shader/dxil/blit_vertex.dxil");
            auto const pixel_binary
                = pr::backend::detail::unique_buffer::create_from_binary_file("res/pr/liveness_sample/shader/dxil/blit_pixel.dxil");

            CC_RUNTIME_ASSERT(vertex_binary.is_valid() && pixel_binary.is_valid() && "failed to load shaders");

            cc::capped_vector<arg::shader_stage, 6> shader_stages;
            shader_stages.push_back(arg::shader_stage{vertex_binary.get(), vertex_binary.size(), shader_domain::vertex});
            shader_stages.push_back(arg::shader_stage{pixel_binary.get(), pixel_binary.size(), shader_domain::pixel});

            cc::capped_vector<format, 8> rtv_formats;
            rtv_formats.push_back(format::rgba8un);

            pr::primitive_pipeline_config config;
            config.cull = pr::cull_mode::front;
            resources.pso_blit
                = backend.createPipelineState(arg::vertex_format{{}, 0}, arg::framebuffer_format{rtv_formats, {}}, payload_shape, shader_stages, config);
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

        {
            cc::capped_vector<shader_view_element, 2> srv_elems;
            srv_elems.emplace_back().init_as_tex2d(resources.colorbuffer, format::rgba16f);
            resources.shaderview_blit = backend.createShaderView(srv_elems);
        }

        auto const on_resize_func = [&](int w, int h) {
            std::cout << "resize to " << w << "x" << h << std::endl;

            backend.flushGPU();

            backend.free(resources.depthbuffer);
            resources.depthbuffer = backend.createRenderTarget(format::depth24un_stencil8u, w, h);
            backend.free(resources.colorbuffer);
            resources.colorbuffer = backend.createRenderTarget(format::rgba16f, w, h);

            backend.free(resources.shaderview_blit);
            cc::capped_vector<shader_view_element, 2> srv_elems;
            srv_elems.emplace_back().init_as_tex2d(resources.colorbuffer, format::rgba16f);
            resources.shaderview_blit = backend.createShaderView(srv_elems);

            backend.resize(w, h);
        };

        // Main loop
        device::Timer timer;
        double run_time = 0.0;


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
                    if (framecounter == 60)
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

                cc::array<handle::command_list, pr_test::num_render_threads + 1> render_cmd_lists;
                cc::fill(render_cmd_lists, handle::null_command_list);

                // clear RTs
                {
                    command_stream_writer cmd_writer(thread_cmd_buffer_mem[0], THREAD_BUFFER_SIZE);

                    cmd::transition_resources cmd_trans;
                    cmd_trans.transitions.push_back(cmd::transition_resources::transition_info{resources.colorbuffer, resource_state::render_target});
                    cmd_writer.add_command(cmd_trans);

                    cmd::begin_render_pass cmd_brp;
                    cmd_brp.viewport = backend.getBackbufferSize();


                    cmd_brp.render_targets.push_back(
                        cmd::begin_render_pass::render_target_info{{}, {0.f, 0.f, 0.f, 1.f}, cmd::begin_render_pass::rt_clear_type::clear});
                    cmd_brp.render_targets.back().sve.init_as_tex2d(resources.colorbuffer, format::rgba16f);


                    cmd_brp.depth_target

                        = cmd::begin_render_pass::depth_stencil_info{{}, 1.f, 0, cmd::begin_render_pass::rt_clear_type::clear};
                    cmd_brp.depth_target.sve.init_as_tex2d(resources.depthbuffer, format::depth24un_stencil8u);

                    cmd_writer.add_command(cmd_brp);

                    cmd_writer.add_command(cmd::end_render_pass{});

                    render_cmd_lists[0] = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
                }

                // parallel rendering
                auto render_sync = td::submit_batched_n(
                    [&](unsigned start, unsigned end, unsigned i) {
                        // cacheline-sized tasks call for desperate measures (macro)
                        command_stream_writer cmd_writer(thread_cmd_buffer_mem[i + 1], THREAD_BUFFER_SIZE);

                        cmd::begin_render_pass cmd_brp;
                        cmd_brp.viewport = backend.getBackbufferSize();

                        cmd_brp.render_targets.push_back(
                            cmd::begin_render_pass::render_target_info{{}, {0.f, 0.f, 0.f, 1.f}, cmd::begin_render_pass::rt_clear_type::load});
                        cmd_brp.render_targets.back().sve.init_as_tex2d(resources.colorbuffer, format::rgba16f);

                        cmd_brp.depth_target = cmd::begin_render_pass::depth_stencil_info{{}, 1.f, 0, cmd::begin_render_pass::rt_clear_type::load};
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


                        render_cmd_lists[i + 1] = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
                    },
                    pr_test::model_matrix_data::num_instances, pr_test::num_render_threads);


                {
                    command_stream_writer cmd_writer(thread_cmd_buffer_mem[0], THREAD_BUFFER_SIZE);

                    auto const ng_backbuffer = backend.acquireBackbuffer();

                    {
                        cmd::transition_resources cmd_trans;
                        cmd_trans.transitions.push_back(cmd::transition_resources::transition_info{ng_backbuffer, resource_state::render_target});
                        cmd_trans.transitions.push_back(cmd::transition_resources::transition_info{resources.colorbuffer, resource_state::shader_resource});
                        cmd_writer.add_command(cmd_trans);
                    }

                    {
                        cmd::begin_render_pass cmd_brp;
                        cmd_brp.viewport = backend.getBackbufferSize();
                        cmd_brp.render_targets.push_back(
                            cmd::begin_render_pass::render_target_info{{}, {0.f, 0.f, 0.f, 1.f}, cmd::begin_render_pass::rt_clear_type::clear});
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
}

TEST("pr::backend::d3d12 liveness", exclusive)
{
    td::launch([&] { run_d3d12_sample(pr_test::get_backend_config()); });
}

TEST("pr::backend::d3d12 adapter choice", exclusive)
{
    auto constexpr mute = false;
    if (0)
    {
        using namespace pr::backend;
        using namespace pr::backend::d3d12;

        // Adapter choice basics
        auto const candidates = get_adapter_candidates();

        auto const vram_choice = get_preferred_adapter_index(candidates, adapter_preference::highest_vram);
        auto const feature_choice = get_preferred_adapter_index(candidates, adapter_preference::highest_feature_level);
        auto const integrated_choice = get_preferred_adapter_index(candidates, adapter_preference::integrated);

        if constexpr (!mute)
        {
            std::cout << "\n\n";
            for (auto const& cand : candidates)
            {
                std::cout << "D3D12 Adapter Candidate #" << cand.index << ": " << cand.description << '\n';
                std::cout << " VRAM: " << unsigned(cand.dedicated_video_memory_bytes / (1024 * 1024)) << "MB";
                std::cout << ", RAM: " << unsigned(cand.dedicated_system_memory_bytes / (1024 * 1024)) << "MB";
                std::cout << ", Shared: " << unsigned(cand.shared_system_memory_bytes / (1024 * 1024)) << "MB\n" << std::endl;
            }


            std::cout << "Chose #" << vram_choice << " for highest vram, #" << feature_choice << " for highest feature level and #"
                      << integrated_choice << " for integrated GPU" << std::endl;
        }
    }
}

#endif
