#include <nexus/test.hh>

#include <iostream>

#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>

#include <typed-geometry/tg.hh>

#include <task-dispatcher/td.hh>

#ifdef PR_BACKEND_D3D12
#include <phantasm-renderer/backend/d3d12/BackendD3D12.hh>
#include <phantasm-renderer/backend/d3d12/adapter_choice_util.hh>
#include <phantasm-renderer/backend/d3d12/common/d3dx12.hh>
#include <phantasm-renderer/backend/d3d12/common/dxgi_format.hh>
#include <phantasm-renderer/backend/d3d12/pipeline_state.hh>
#include <phantasm-renderer/backend/d3d12/pools/cmd_list_pool.hh>
#include <phantasm-renderer/backend/d3d12/root_signature.hh>
#endif

#ifdef PR_BACKEND_VULKAN
#include <phantasm-renderer/backend/vulkan/BackendVulkan.hh>
#include <phantasm-renderer/backend/vulkan/CommandBufferRing.hh>
#include <phantasm-renderer/backend/vulkan/common/util.hh>
#include <phantasm-renderer/backend/vulkan/common/verify.hh>
#include <phantasm-renderer/backend/vulkan/common/zero_struct.hh>
#include <phantasm-renderer/backend/vulkan/memory/Allocator.hh>
#include <phantasm-renderer/backend/vulkan/memory/DynamicBufferRing.hh>
#include <phantasm-renderer/backend/vulkan/memory/UploadHeap.hh>
#include <phantasm-renderer/backend/vulkan/memory/VMA.hh>
#include <phantasm-renderer/backend/vulkan/pipeline_state.hh>
#include <phantasm-renderer/backend/vulkan/resources/resource_creation.hh>
#include <phantasm-renderer/backend/vulkan/resources/resource_state.hh>
#include <phantasm-renderer/backend/vulkan/resources/resource_view.hh>
#include <phantasm-renderer/backend/vulkan/resources/vertex_attributes.hh>
#include <phantasm-renderer/backend/vulkan/shader.hh>
#include <phantasm-renderer/backend/vulkan/shader_arguments.hh>
#endif

#include <phantasm-renderer/backend/assets/image_loader.hh>
#include <phantasm-renderer/backend/assets/mesh_loader.hh>
#include <phantasm-renderer/backend/assets/vertex_attrib_info.hh>
#include <phantasm-renderer/backend/command_stream.hh>
#include <phantasm-renderer/backend/device_tentative/timer.hh>
#include <phantasm-renderer/backend/device_tentative/window.hh>
#include <phantasm-renderer/default_config.hh>


namespace
{
auto const get_projection_matrix = [](int w, int h) -> tg::mat4 { return tg::perspective_directx(60_deg, w / float(h), 0.1f, 100.f); };

auto const get_view_matrix = [](double runtime) -> tg::mat4 {
    constexpr auto target = tg::pos3(0, 1.45f, 0);
    const auto cam_pos
        = tg::rotate_y(tg::pos3(1, 1.5f, 1) * 4.f, tg::radians(float(runtime * 0.01))) + tg::vec3(0, tg::sin(tg::radians(float(runtime * 0.25))), 0);
    return tg::look_at_directx(cam_pos, target, tg::vec3(0, 1, 0));
};

auto const get_view_projection_matrix = [](double runtime, int w, int h) -> tg::mat4 { return get_projection_matrix(w, h) * get_view_matrix(runtime); };

auto const get_model_matrix = [](tg::vec3 pos, double runtime, unsigned index) -> tg::mat4 {
    constexpr auto model_scale = 1.25f;
    return tg::translation(pos) * tg::rotation_y(tg::radians((float(runtime) + float(index) * 0.5f * tg::pi_scalar<float>)*(index % 2 == 0 ? -1 : 1)))
           * tg::scaling(model_scale, model_scale, model_scale);
};

constexpr auto sample_mesh_path = "res/pr/liveness_sample/mesh/apollo.obj";
constexpr auto sample_texture_path = "res/pr/liveness_sample/texture/uv_checker.png";
constexpr auto num_render_threads = 8;

struct model_matrix_data
{
    static constexpr auto num_instances = 16;

    struct padded_instance
    {
        tg::mat4 model_mat;
        char padding[256 - sizeof(tg::mat4)];
    };

    static_assert(sizeof(padded_instance) == 256);
    cc::array<padded_instance, num_instances> model_matrices;

    void fill(double runtime)
    {
        cc::array constexpr model_positions
            = {tg::vec3(1, 0, 0), tg::vec3(0, 1, 0), tg::vec3(0, 0, 1), tg::vec3(-1, 0, 0), tg::vec3(0, -1, 0), tg::vec3(0, 0, -1)};

        auto mp_i = 0u;
        for (auto i = 0u; i < num_instances; ++i)
        {
            model_matrices[i].model_mat = get_model_matrix(model_positions[mp_i] * 0.5f * float(i), runtime / (double(i + 1) * .25), i);


            ++mp_i;
            if (mp_i == model_positions.size())
                mp_i -= model_positions.size();
        }
    }
};

#ifdef PR_BACKEND_D3D12
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
    window.initialize("Liveness test | D3D12");

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
                auto const img_data = assets::load_image(sample_texture_path, img_size);
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
                auto const mesh_data = assets::load_obj_mesh(sample_mesh_path);
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
            format const dsv_format = format::depth32f;

            resources.pso_render = backend.createPipelineState(arg::vertex_format{attrib_info, sizeof(assets::simple_vertex)},
                                                               arg::framebuffer_format{rtv_formats, cc::span{dsv_format}}, payload_shape,
                                                               shader_stages, pr::default_config);
        }

        {
            cc::capped_vector<arg::shader_argument_shape, 4> payload_shape;
            {
                // Argument 1, blit target SRV
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

        resources.shaderview_render = backend.createShaderView(cc::span{resources.material});

        resources.cb_camdata = backend.createMappedBuffer(sizeof(tg::mat4));
        std::byte* const cb_camdata_map = backend.getMappedMemory(resources.cb_camdata);

        resources.cb_modeldata = backend.createMappedBuffer(sizeof(model_matrix_data));
        std::byte* const cb_modeldata_map = backend.getMappedMemory(resources.cb_modeldata);

        resources.depthbuffer = backend.createRenderTarget(format::depth32f, 150, 150);
        resources.colorbuffer = backend.createRenderTarget(format::rgba16f, 150, 150);

        handle::shader_view shaderview_blit = backend.createShaderView(cc::span{resources.colorbuffer});

        auto const on_resize_func = [&](int w, int h) {
            std::cout << "resize to " << w << "x" << h << std::endl;

            backend.free(resources.depthbuffer);
            resources.depthbuffer = backend.createRenderTarget(format::depth32f, w, h);
            backend.free(resources.colorbuffer);
            resources.colorbuffer = backend.createRenderTarget(format::rgba16f, w, h);

            backend.free(shaderview_blit);
            shaderview_blit = backend.createShaderView(cc::span{resources.colorbuffer});

            backend.resize(w, h);
        };

        // Main loop
        device::Timer timer;
        double run_time = 0.0;


#define THREAD_BUFFER_SIZE (static_cast<size_t>(1024ull * 10))
        cc::array<std::byte*, num_render_threads> thread_cmd_buffer_mem;

        for (auto& mem : thread_cmd_buffer_mem)
            mem = static_cast<std::byte*>(std::malloc(THREAD_BUFFER_SIZE));

        CC_DEFER
        {
            for (std::byte* mem : thread_cmd_buffer_mem)
                std::free(mem);
        };


        model_matrix_data model_data;

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
                auto const frametime = timer.elapsedSecondsD();
                timer.restart();
                run_time += frametime;

                // Data upload
                {
                    auto const vp = get_view_projection_matrix(run_time, window.getWidth(), window.getHeight());
                    std::memcpy(cb_camdata_map, &vp, sizeof(vp));

                    model_data.fill(run_time);
                    std::memcpy(cb_modeldata_map, &model_data, sizeof(model_data));
                }

                cc::array<handle::command_list, num_render_threads + 1> render_cmd_lists;
                cc::fill(render_cmd_lists, handle::null_command_list);

                // clear RTs
                {
                    command_stream_writer cmd_writer(thread_cmd_buffer_mem[0], THREAD_BUFFER_SIZE);

                    cmd::transition_resources cmd_trans;
                    cmd_trans.transitions.push_back(cmd::transition_resources::transition_info{resources.colorbuffer, resource_state::render_target});
                    cmd_writer.add_command(cmd_trans);

                    cmd::begin_render_pass cmd_brp;
                    cmd_brp.viewport = backend.getBackbufferSize();
                    cmd_brp.render_targets.push_back(cmd::begin_render_pass::render_target_info{
                        resources.colorbuffer, {0.f, 0.f, 0.f, 1.f}, cmd::begin_render_pass::rt_clear_type::clear});
                    cmd_brp.depth_target
                        = cmd::begin_render_pass::depth_stencil_info{resources.depthbuffer, 1.f, 0, cmd::begin_render_pass::rt_clear_type::clear};
                    cmd_writer.add_command(cmd_brp);

                    cmd_writer.add_command(cmd::end_render_pass{});

                    render_cmd_lists[0] = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
                }

                // parallel rendering
                {
                    auto render_sync = td::submit_batched_n(
                        [&](unsigned start, unsigned end, unsigned i) {
                            // cacheline-sized tasks call for desperate measures (macro)
                            command_stream_writer cmd_writer(thread_cmd_buffer_mem[i], THREAD_BUFFER_SIZE);

                            cmd::begin_render_pass cmd_brp;
                            cmd_brp.viewport = backend.getBackbufferSize();
                            cmd_brp.render_targets.push_back(cmd::begin_render_pass::render_target_info{
                                resources.colorbuffer, {0.f, 0.f, 0.f, 1.f}, cmd::begin_render_pass::rt_clear_type::load});
                            cmd_brp.depth_target
                                = cmd::begin_render_pass::depth_stencil_info{resources.depthbuffer, 1.f, 0, cmd::begin_render_pass::rt_clear_type::load};
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
                                cmd_draw.shader_arguments[1].constant_buffer_offset = sizeof(model_matrix_data::padded_instance) * inst;
                                cmd_writer.add_command(cmd_draw);
                            }

                            cmd_writer.add_command(cmd::end_render_pass{});


                            render_cmd_lists[i + 1] = backend.recordCommandList(cmd_writer.buffer(), cmd_writer.size());
                        },
                        model_matrix_data::num_instances, num_render_threads);


                    td::wait_for(render_sync);
                    backend.submit(render_cmd_lists);
                }

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
                        cmd_brp.render_targets.push_back(cmd::begin_render_pass::render_target_info{
                            ng_backbuffer, {0.f, 0.f, 0.f, 1.f}, cmd::begin_render_pass::rt_clear_type::clear});
                        cmd_writer.add_command(cmd_brp);
                    }

                    {
                        cmd::draw cmd_draw;
                        cmd_draw.num_indices = 3;
                        cmd_draw.index_buffer = handle::null_resource;
                        cmd_draw.vertex_buffer = handle::null_resource;
                        cmd_draw.pipeline_state = resources.pso_blit;
                        cmd_draw.shader_arguments.push_back(shader_argument{handle::null_resource, 0, shaderview_blit});
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
#endif
}

TEST("pr backend liveness", exclusive)
{
    pr::backend::backend_config config;
    config.validation = pr::backend::validation_level::on_extended;
    config.adapter_preference = pr::backend::adapter_preference::highest_vram;
    config.num_threads = td::system::hardware_concurrency;

#ifdef PR_BACKEND_D3D12
    td::launch([&] { run_d3d12_sample(config); });
#endif

#ifdef PR_BACKEND_VULKAN
    {
        using namespace pr::backend;
        using namespace pr::backend::vk;

        device::Window window;
        window.initialize("Liveness test | Vulkan");

        BackendVulkan bv;
        bv.initialize(config, window);

        CommandBufferRing commandBufferRing;
        DynamicBufferRing dynamicBufferRing;
        UploadHeap uploadHeap;
        DescriptorAllocator descAllocator;
        {
            auto const num_backbuffers = bv.mSwapchain.getNumBackbuffers();
            auto const num_command_lists = 8;
            auto const num_cbvs = 2000;
            auto const num_srvs = 2000;
            auto const num_uavs = 20;
            //            auto const num_dsvs = 3;
            //            auto const num_rtvs = 60;
            auto const num_samplers = 20;
            auto const dynamic_buffer_size = 20 * 1024 * 1024;
            auto const upload_size = 1000 * 1024 * 1024;

            commandBufferRing.initialize(bv.mDevice, num_backbuffers, num_command_lists, bv.mDevice.getQueueFamilyGraphics());
            dynamicBufferRing.initialize(&bv.mDevice, &bv.mAllocator, num_backbuffers, dynamic_buffer_size);
            uploadHeap.initialize(&bv.mDevice, upload_size);
            descAllocator.initialize(&bv.mDevice, num_cbvs, num_srvs, num_uavs, num_samplers);
        }

        pipeline_layout arc_pipeline_layout;
        {
            shader_payload_shape payload_shape;

            // Argument 0, camera CBV
            {
                shader_payload_shape::shader_argument_shape arg_shape;
                arg_shape.has_cb = true;
                arg_shape.num_srvs = 0;
                arg_shape.num_uavs = 0;
                payload_shape.shader_arguments.push_back(arg_shape);
            }

            // Argument 1, pixel shader SRV and model matrix CBV
            {
                shader_payload_shape::shader_argument_shape arg_shape;
                arg_shape.has_cb = true;
                arg_shape.num_srvs = 1;
                arg_shape.num_uavs = 0;
                payload_shape.shader_arguments.push_back(arg_shape);
            }

            arc_pipeline_layout.initialize(bv.mDevice.getDevice(), payload_shape);
        }

        pipeline_layout present_pipeline_layout;
        {
            shader_payload_shape payload_shape;

            // Argument 0, color target SRV
            {
                shader_payload_shape::shader_argument_shape arg_shape;
                arg_shape.has_cb = false;
                arg_shape.num_srvs = 1;
                arg_shape.num_uavs = 0;
                payload_shape.shader_arguments.push_back(arg_shape);
            }

            present_pipeline_layout.initialize(bv.mDevice.getDevice(), payload_shape);
        }

        image depth_image;
        image color_rt_image;
        VkImageView depth_view;
        VkImageView color_rt_view;
        {
            depth_image = create_depth_stencil(bv.mAllocator, 15u, 15u, VK_FORMAT_D32_SFLOAT);
            depth_view = make_image_view(bv.mDevice.getDevice(), depth_image, VK_FORMAT_D32_SFLOAT);

            color_rt_image = create_render_target(bv.mAllocator, 15u, 15u, VK_FORMAT_R16G16B16A16_SFLOAT);
            color_rt_view = make_image_view(bv.mDevice.getDevice(), color_rt_image, VK_FORMAT_R16G16B16A16_SFLOAT);
        }

        VkRenderPass arcRenderPass;
        {
            wip::framebuffer_format framebuffer_format;
            framebuffer_format.render_targets.push_back(VK_FORMAT_R16G16B16A16_SFLOAT);
            framebuffer_format.depth_target.push_back(VK_FORMAT_D32_SFLOAT);

            auto arc_prim_config = pr::primitive_pipeline_config{};
            arcRenderPass = create_render_pass(bv.mDevice.getDevice(), framebuffer_format, arc_prim_config);
        }

        cc::capped_vector<shader, 6> arcShaders;
        arcShaders.push_back(create_shader_from_spirv_file(bv.mDevice.getDevice(), "res/pr/liveness_sample/shader/spirv/pixel.spv", shader_domain::pixel));
        arcShaders.push_back(create_shader_from_spirv_file(bv.mDevice.getDevice(), "res/pr/liveness_sample/shader/spirv/vertex.spv", shader_domain::vertex));

        VkPipeline arcPipeline;
        VkFramebuffer arcFramebuffer;
        {
            auto const vert_attribs = assets::get_vertex_attributes<assets::simple_vertex>();
            auto const input_layout = get_input_description(vert_attribs);
            arcPipeline = create_pipeline(bv.mDevice.getDevice(), arcRenderPass, arc_pipeline_layout.pipeline_layout, arcShaders, pr::default_config,
                                          input_layout, sizeof(assets::simple_vertex));

            {
                cc::array const attachments = {color_rt_view, depth_view};
                VkFramebufferCreateInfo fb_info;
                zero_info_struct(fb_info, VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
                fb_info.pNext = nullptr;
                fb_info.renderPass = arcRenderPass;
                fb_info.attachmentCount = unsigned(attachments.size());
                fb_info.pAttachments = attachments.data();
                fb_info.width = 15;
                fb_info.height = 15;
                fb_info.layers = 1;
                PR_VK_VERIFY_SUCCESS(vkCreateFramebuffer(bv.mDevice.getDevice(), &fb_info, nullptr, &arcFramebuffer));
            }
        }

        cc::capped_vector<shader, 6> presentShaders;
        presentShaders.push_back(create_shader_from_spirv_file(bv.mDevice.getDevice(), "res/pr/liveness_sample/shader/spirv/pixel_blit.spv", shader_domain::pixel));
        presentShaders.push_back(create_shader_from_spirv_file(bv.mDevice.getDevice(), "res/pr/liveness_sample/shader/spirv/vertex_blit.spv", shader_domain::vertex));

        VkPipeline presentPipeline;
        {
            presentPipeline
                = create_fullscreen_pipeline(bv.mDevice.getDevice(), bv.mSwapchain.getRenderPass(), present_pipeline_layout.pipeline_layout, presentShaders);
        }

        buffer vert_buf;
        buffer ind_buf;
        unsigned num_indices;

        {
            auto const mesh_data = assets::load_obj_mesh(sample_mesh_path);
            num_indices = unsigned(mesh_data.indices.size());

            vert_buf = create_vertex_buffer_from_data<assets::simple_vertex>(bv.mAllocator, uploadHeap, mesh_data.vertices);
            ind_buf = create_index_buffer_from_data<int>(bv.mAllocator, uploadHeap, mesh_data.indices);
        }

        image albedo_image;
        VkImageView albedo_view;
        {
            albedo_image = create_texture_from_file(bv.mAllocator, uploadHeap, sample_texture_path);
            albedo_view = make_image_view(bv.mDevice.getDevice(), albedo_image, VK_FORMAT_R8G8B8A8_UNORM);
        }

        uploadHeap.flushAndFinish();

        descriptor_set_bundle arc_desc_set;
        {
            // we just need a single one of these, since we do not update it during dispatch
            // we just re-bind at different offsets

            arc_desc_set.initialize(descAllocator, arc_pipeline_layout);

            legacy::shader_argument shader_arg_zero;
            shader_arg_zero.cbv = dynamicBufferRing.getBuffer();
            shader_arg_zero.cbv_view_offset = 0;
            shader_arg_zero.cbv_view_size = sizeof(tg::mat4);

            arc_desc_set.update_argument(bv.mDevice.getDevice(), 0, shader_arg_zero);

            legacy::shader_argument shader_arg_one;
            shader_arg_one.srvs.push_back(albedo_view);
            shader_arg_one.cbv = dynamicBufferRing.getBuffer();
            shader_arg_one.cbv_view_offset = 0;
            shader_arg_one.cbv_view_size = sizeof(tg::mat4);

            arc_desc_set.update_argument(bv.mDevice.getDevice(), 1, shader_arg_one);
        }


        descriptor_set_bundle present_desc_set;
        {
            present_desc_set.initialize(descAllocator, present_pipeline_layout);

            legacy::shader_argument arg_zero;
            arg_zero.srvs.push_back(color_rt_view);
            present_desc_set.update_argument(bv.mDevice.getDevice(), 0, arg_zero);
        }

        auto const on_swapchain_resize_func = [&](tg::ivec2 backbuffer_size) {
            // recreate depth buffer
            bv.mAllocator.free(depth_image);
            depth_image = create_depth_stencil(bv.mAllocator, unsigned(backbuffer_size.x), unsigned(backbuffer_size.y), VK_FORMAT_D32_SFLOAT);
            vkDestroyImageView(bv.mDevice.getDevice(), depth_view, nullptr);
            depth_view = make_image_view(bv.mDevice.getDevice(), depth_image, VK_FORMAT_D32_SFLOAT);

            // recreate color rt buffer
            bv.mAllocator.free(color_rt_image);
            color_rt_image = create_render_target(bv.mAllocator, unsigned(backbuffer_size.x), unsigned(backbuffer_size.y), VK_FORMAT_R16G16B16A16_SFLOAT);
            vkDestroyImageView(bv.mDevice.getDevice(), color_rt_view, nullptr);
            color_rt_view = make_image_view(bv.mDevice.getDevice(), color_rt_image, VK_FORMAT_R16G16B16A16_SFLOAT);

            // update RT-dependent descriptor sets
            {
                legacy::shader_argument arg_zero;
                arg_zero.srvs.push_back(color_rt_view);
                present_desc_set.update_argument(bv.mDevice.getDevice(), 0, arg_zero);
            }

            // transition RTs
            {
                auto const cmd_buf = commandBufferRing.acquireCommandBuffer();

                barrier_bundle<2> barrier_dispatch;
                barrier_dispatch.add_image_barrier(color_rt_image.image,
                                                   state_change{resource_state::undefined, resource_state::shader_resource, shader_domain::pixel});
                barrier_dispatch.add_image_barrier(depth_image.image, state_change{resource_state::undefined, resource_state::depth_write}, true);
                barrier_dispatch.submit(cmd_buf, bv.mDevice.getQueueGraphics());
            }

            // recreate arc framebuffer
            {
                vkDestroyFramebuffer(bv.mDevice.getDevice(), arcFramebuffer, nullptr);
                cc::array const attachments = {color_rt_view, depth_view};
                VkFramebufferCreateInfo fb_info = {};
                fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fb_info.pNext = nullptr;
                fb_info.renderPass = arcRenderPass;
                fb_info.attachmentCount = unsigned(attachments.size());
                fb_info.pAttachments = attachments.data();
                fb_info.width = unsigned(backbuffer_size.x);
                fb_info.height = unsigned(backbuffer_size.y);
                fb_info.layers = 1;
                PR_VK_VERIFY_SUCCESS(vkCreateFramebuffer(bv.mDevice.getDevice(), &fb_info, nullptr, &arcFramebuffer));
            }

            // flush
            vkDeviceWaitIdle(bv.mDevice.getDevice());
            std::cout << "resized swapchain to: " << backbuffer_size.x << "x" << backbuffer_size.y << std::endl;
        };

        auto const on_window_resize_func = [&](int width, int height) {
            bv.mSwapchain.onResize(width, height);
            std::cout << "resized window to " << width << "x" << height << std::endl;
        };

        device::Timer timer;
        double run_time = 0.0;

        while (!window.isRequestingClose())
        {
            window.pollEvents();
            if (window.isPendingResize())
            {
                if (!window.isMinimized())
                {
                    on_window_resize_func(window.getWidth(), window.getHeight());
                }
                window.clearPendingResize();
            }

            if (!window.isMinimized())
            {
                if (bv.mSwapchain.hasBackbufferResized())
                {
                    // The swapchain has resized, recreate depending resources
                    on_swapchain_resize_func(bv.mSwapchain.getBackbufferSize());
                    bv.mSwapchain.clearBackbufferResizeFlag();
                }

                auto const frametime = timer.elapsedSecondsD();
                timer.restart();
                run_time += frametime;

                if (!bv.mSwapchain.waitForBackbuffer())
                {
                    // The swapchain was out of date and has resized, discard this frame
                    continue;
                }

                commandBufferRing.onBeginFrame();
                dynamicBufferRing.onBeginFrame();

                auto const backbuffer_size = bv.mSwapchain.getBackbufferSize();

                auto const cmd_buf = commandBufferRing.acquireCommandBuffer();

                {
                    // transition color_rt to render target
                    barrier_bundle<1> barrier;
                    barrier.add_image_barrier(color_rt_image.image,
                                              state_change{resource_state::shader_resource, shader_domain::pixel, resource_state::render_target});
                    barrier.record(cmd_buf);
                }

                // Arc render pass
                {
                    VkRenderPassBeginInfo renderPassInfo = {};
                    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    renderPassInfo.renderPass = arcRenderPass;
                    renderPassInfo.framebuffer = arcFramebuffer;
                    renderPassInfo.renderArea.offset = {0, 0};
                    renderPassInfo.renderArea.extent.width = backbuffer_size.x;
                    renderPassInfo.renderArea.extent.height = backbuffer_size.y;

                    cc::array<VkClearValue, 2> clear_values;
                    clear_values[0] = {0.f, 0.f, 0.f, 1.f};
                    clear_values[1] = {1.f, 0};

                    renderPassInfo.clearValueCount = clear_values.size();
                    renderPassInfo.pClearValues = clear_values.data();

                    util::set_viewport(cmd_buf, backbuffer_size);
                    vkCmdBeginRenderPass(cmd_buf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, arcPipeline);

                    VkBuffer vertex_buffers[] = {vert_buf.buffer};
                    VkDeviceSize offsets[] = {0};

                    vkCmdBindVertexBuffers(cmd_buf, 0, 1, vertex_buffers, offsets);
                    vkCmdBindIndexBuffer(cmd_buf, ind_buf.buffer, 0, VK_INDEX_TYPE_UINT32);


                    {
                        VkDescriptorBufferInfo cb_upload_info;
                        {
                            struct mvp_data
                            {
                                tg::mat4 vp;
                            };

                            {
                                mvp_data* data;
                                dynamicBufferRing.allocConstantBufferTyped(data, cb_upload_info);
                                data->vp = get_view_projection_matrix(run_time, window.getWidth(), window.getHeight());
                            }
                        }

                        auto const dynamic_offset = uint32_t(cb_upload_info.offset);

                        arc_desc_set.bind_argument(cmd_buf, arc_pipeline_layout.pipeline_layout, 0, dynamic_offset);
                    }

                    {
                        model_matrix_data* vert_cb_data;
                        VkDescriptorBufferInfo cb_upload_info;
                        dynamicBufferRing.allocConstantBufferTyped(vert_cb_data, cb_upload_info);

                        // record model matrices
                        vert_cb_data->fill(run_time);

                        for (auto i = 0u; i < vert_cb_data->model_matrices.size(); ++i)
                        {
                            auto const dynamic_offset = uint32_t(i * sizeof(vert_cb_data->model_matrices[0]));
                            auto const combined_offset = uint32_t(cb_upload_info.offset) + dynamic_offset;

                            arc_desc_set.bind_argument(cmd_buf, arc_pipeline_layout.pipeline_layout, 1, combined_offset);
                            vkCmdDrawIndexed(cmd_buf, num_indices, 1, 0, 0, 0);
                        }
                    }

                    vkCmdEndRenderPass(cmd_buf);
                }

                {
                    // transition color_rt to shader resource
                    barrier_bundle<1> barrier;
                    barrier.add_image_barrier(color_rt_image.image,
                                              state_change{resource_state::render_target, resource_state::shader_resource, shader_domain::pixel});
                    barrier.record(cmd_buf);
                }

                // Present render pass
                {
                    VkRenderPassBeginInfo renderPassInfo = {};
                    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    renderPassInfo.renderPass = bv.mSwapchain.getRenderPass();
                    renderPassInfo.framebuffer = bv.mSwapchain.getCurrentFramebuffer();
                    renderPassInfo.renderArea.offset = {0, 0};
                    renderPassInfo.renderArea.extent.width = backbuffer_size.x;
                    renderPassInfo.renderArea.extent.height = backbuffer_size.y;

                    cc::array<VkClearValue, 1> clear_values;
                    clear_values[0] = {0.f, 0.f, 0.f, 1.f};

                    renderPassInfo.clearValueCount = clear_values.size();
                    renderPassInfo.pClearValues = clear_values.data();

                    util::set_viewport(cmd_buf, backbuffer_size);
                    vkCmdBeginRenderPass(cmd_buf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipeline);

                    present_desc_set.bind_argument(cmd_buf, present_pipeline_layout.pipeline_layout, 0);

                    vkCmdDraw(cmd_buf, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd_buf);
                }

                PR_VK_VERIFY_SUCCESS(vkEndCommandBuffer(cmd_buf));

                bv.mSwapchain.performPresentSubmit(cmd_buf);
                bv.mSwapchain.present();
            }
        }

        {
            auto const& device = bv.mDevice.getDevice();

            vkDeviceWaitIdle(device);

            bv.mAllocator.free(vert_buf);
            bv.mAllocator.free(ind_buf);

            bv.mAllocator.free(albedo_image);
            vkDestroyImageView(device, albedo_view, nullptr);

            bv.mAllocator.free(depth_image);
            vkDestroyImageView(device, depth_view, nullptr);

            bv.mAllocator.free(color_rt_image);
            vkDestroyImageView(device, color_rt_view, nullptr);

            vkDestroyFramebuffer(device, arcFramebuffer, nullptr);
            vkDestroyPipeline(device, arcPipeline, nullptr);
            vkDestroyRenderPass(device, arcRenderPass, nullptr);

            vkDestroyPipeline(device, presentPipeline, nullptr);

            arc_pipeline_layout.free(device);
            present_pipeline_layout.free(device);

            arc_desc_set.free(descAllocator);
            present_desc_set.free(descAllocator);

            for (auto& shader : arcShaders)
                shader.free(device);

            for (auto& shader : presentShaders)
                shader.free(device);
        }
    }
#endif
}

TEST("pr adapter choice", exclusive)
{
#ifdef PR_BACKEND_D3D12
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
#endif
}
