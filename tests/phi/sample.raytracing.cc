#include "sample.hh"

#include <cmath>
#include <cstdio>

#include <rich-log/log.hh>

#include <clean-core/capped_array.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/commands.hh>
#include <phantasm-hardware-interface/detail/byte_util.hh>
#include <phantasm-hardware-interface/detail/unique_buffer.hh>
#include <phantasm-hardware-interface/window_handle.hh>

#include <arcana-incubator/asset-loading/image_loader.hh>
#include <arcana-incubator/asset-loading/mesh_loader.hh>
#include <arcana-incubator/device-abstraction/device_abstraction.hh>
#include <arcana-incubator/device-abstraction/freefly_camera.hh>
#include <arcana-incubator/device-abstraction/input.hh>
#include <arcana-incubator/device-abstraction/timer.hh>
#include <arcana-incubator/pr-util/demo-renderer/data.hh>

#include <arcana-incubator/imgui/imgui.hh>
#include <arcana-incubator/imgui/imgui_impl_phi.hh>

#include "sample_util.hh"
#include "scene.hh"

void phi_test::run_raytracing_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& backend_config)
{
    using namespace phi;
    // backend init
    auto conf = backend_config;
    conf.enable_raytracing = false;
    backend.initialize(conf);

    if (!backend.isRaytracingEnabled())
    {
        LOG_WARN("current GPU has no raytracing capabilities");
        std::getchar();
        return;
    }

    // window init
    inc::da::initialize();
    inc::da::SDLWindow window;
    window.initialize(sample_config.window_title);
    initialize_imgui(window, backend);

    // main swapchain creation
    phi::handle::swapchain const main_swapchain = backend.createSwapchain({window.getSdlWindow()}, window.getSize());
    // unsigned const msc_num_backbuffers = backend.getNumBackbuffers(main_swapchain);
    phi::format const msc_backbuf_format = backend.getBackbufferFormat(main_swapchain);


    inc::da::input_manager input;
    input.initialize();
    inc::da::smooth_fps_cam camera;
    inc::pre::dmr::camera_gpudata camera_data;
    camera.setup_default_inputs(input);

    struct resources_t
    {
        handle::resource b_camdata_stacked = handle::null_resource;
        unsigned camdata_stacked_offset = 0;

        handle::resource vertex_buffer = handle::null_resource;
        handle::resource index_buffer = handle::null_resource;
        unsigned num_indices = 0;
        unsigned num_vertices = 0;

        handle::accel_struct blas = handle::null_accel_struct;
        uint64_t blas_native = 0;
        handle::accel_struct tlas = handle::null_accel_struct;

        handle::pipeline_state rt_pso = handle::null_pipeline_state;
        handle::resource rt_write_texture = handle::null_resource;

        handle::resource shader_table_raygen = handle::null_resource;

        handle::shader_view raygen_shader_view = handle::null_shader_view;

        handle::resource shader_table_miss = handle::null_resource;
        handle::resource shader_table_hitgroups = handle::null_resource;

    } resources;

    unsigned backbuf_index = 0;
    tg::isize2 backbuf_size = tg::isize2(50, 50);
    shader_table_sizes table_sizes;

    // Res setup
    {
        auto const buffer_size = 1024ull * 2;
        auto* const buffer = static_cast<std::byte*>(std::malloc(buffer_size));
        CC_DEFER { std::free(buffer); };
        command_stream_writer writer(buffer, buffer_size);

        // camdata buffer
        {
            unsigned const size_camdata_256 = phi::util::align_up(sizeof(inc::pre::dmr::camera_gpudata), 256);
            resources.b_camdata_stacked = backend.createUploadBuffer(size_camdata_256 * 3, size_camdata_256);
            resources.camdata_stacked_offset = size_camdata_256;
        }

        // Mesh setup
        {
            writer.reset();
            handle::resource upload_buffer;
            {
                auto const mesh_data = phi_test::sample_mesh_binary ? inc::assets::load_binary_mesh(phi_test::sample_mesh_path)
                                                                    : inc::assets::load_obj_mesh(phi_test::sample_mesh_path);

                resources.num_indices = unsigned(mesh_data.indices.size());
                resources.num_vertices = unsigned(mesh_data.vertices.size());

                auto const vert_size = mesh_data.vertices.size_bytes();
                auto const ind_size = mesh_data.indices.size_bytes();

                resources.vertex_buffer = backend.createBuffer(vert_size, sizeof(inc::assets::simple_vertex));
                resources.index_buffer = backend.createBuffer(ind_size, sizeof(int));

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(resources.vertex_buffer, resource_state::copy_dest);
                    tcmd.add(resources.index_buffer, resource_state::copy_dest);
                    writer.add_command(tcmd);
                }

                upload_buffer = backend.createUploadBuffer(vert_size + ind_size);
                std::byte* const upload_mapped = backend.mapBuffer(upload_buffer);

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

            auto const meshupload_list = backend.recordCommandList(writer.buffer(), writer.size());

            backend.unmapBuffer(upload_buffer);
            backend.submit(cc::span{meshupload_list});
            backend.flushGPU();
            backend.free(upload_buffer);
        }

        // AS / RT setup
        {
            writer.reset();
            {
                constexpr unsigned num_instances = 2;
                arg::blas_element blas_elements[num_instances];

                for (auto i = 0u; i < num_instances; ++i)
                {
                    auto& elem = blas_elements[i];
                    elem.is_opaque = true;
                    elem.index_buffer = resources.index_buffer;
                    elem.vertex_buffer = resources.vertex_buffer;
                    elem.num_indices = resources.num_indices;
                    elem.num_vertices = resources.num_vertices;
                }

                resources.blas = backend.createBottomLevelAccelStruct(
                    blas_elements, accel_struct_build_flags::prefer_fast_trace | accel_struct_build_flags::allow_compaction, &resources.blas_native);
                resources.tlas = backend.createTopLevelAccelStruct(num_instances);


                accel_struct_geometry_instance instance_data[num_instances];

                for (auto i = 0u; i < num_instances; ++i)
                {
                    auto& inst = instance_data[i];
                    inst.instance_id = i;
                    inst.mask = 0xFF;
                    inst.hit_group_index = 0;
                    inst.flags = accel_struct_instance_flags::triangle_front_counterclockwise;
                    inst.native_accel_struct_handle = resources.blas_native;

                    tg::mat4 const transform
                        = tg::transpose(tg::translation<float>(i * 20, i * 20, 0) * tg::rotation_y(0_deg) /* * tg::scaling(.1f, .1f, .1f)*/);
                    std::memcpy(inst.transposed_transform, tg::data_ptr(transform), sizeof(inst.transposed_transform));
                }

                backend.uploadTopLevelInstances(resources.tlas, instance_data);

                cmd::update_bottom_level bcmd;
                bcmd.dest = resources.blas;
                bcmd.source = handle::null_accel_struct;
                writer.add_command(bcmd);

                cmd::update_top_level tcmd;
                tcmd.dest = resources.tlas;
                tcmd.num_instances = num_instances;
                writer.add_command(tcmd);
            }

            auto const accelstruct_cmdlist = backend.recordCommandList(writer.buffer(), writer.size());
            backend.submit(cc::span{accelstruct_cmdlist});
            backend.flushGPU();
        }
    }

    // PSO setup
    {
        cc::capped_vector<detail::unique_buffer, 16> shader_binaries;
        cc::capped_vector<arg::raytracing_shader_library, 16> libraries;

        {
            shader_binaries.push_back(get_shader_binary("raytrace_lib", sample_config.shader_ending));
            CC_RUNTIME_ASSERT(shader_binaries.back().is_valid() && "failed to load raytracing_lib shader");
            auto& main_lib = libraries.emplace_back();
            main_lib.binary = {shader_binaries.back().get(), shader_binaries.back().size()};
            main_lib.exports = {{shader_stage::ray_gen, "raygeneration"}, {shader_stage::ray_miss, "miss"}, {shader_stage::ray_closest_hit, "closesthit"}};
        }

        cc::capped_vector<arg::raytracing_argument_association, 16> arg_assocs;

        {
            auto& raygen_assoc = arg_assocs.emplace_back();
            raygen_assoc.library_index = 0;
            raygen_assoc.export_indices = {0}; // raygeneration
            raygen_assoc.argument_shapes.push_back(arg::shader_arg_shape{1, 1, 0, true});
            raygen_assoc.has_root_constants = false;

            auto& closesthit_assoc = arg_assocs.emplace_back();
            closesthit_assoc.library_index = 0;
            closesthit_assoc.export_indices = {2}; // closesthit
            closesthit_assoc.argument_shapes.push_back(arg::shader_arg_shape{1, 0, 0, false});
        }

        arg::raytracing_hit_group main_hit_group;
        main_hit_group.name = "primary_hitgroup";
        main_hit_group.closest_hit_name = "closesthit";

        resources.rt_pso = backend.createRaytracingPipelineState(libraries, arg_assocs, cc::span{main_hit_group}, 4, sizeof(float[4]), sizeof(float[2]));
    }

    auto const f_free_sized_resources = [&] {
        backend.free(resources.rt_write_texture);

        backend.free(resources.raygen_shader_view);
        backend.free(resources.shader_table_raygen);
        backend.free(resources.shader_table_miss);
        backend.free(resources.shader_table_hitgroups);
    };

    auto const f_create_sized_resources = [&] {
        // Create RT write texture
        resources.rt_write_texture = backend.createTexture(msc_backbuf_format, backbuf_size, 1, texture_dimension::t2d, 1, true);

        // Shader table setup
        {
            {
                resource_view uav_sve;
                uav_sve.init_as_tex2d(resources.rt_write_texture, msc_backbuf_format);

                resource_view srv_sve;
                srv_sve.init_as_accel_struct(backend.getAccelStructBuffer(resources.tlas));

                resources.raygen_shader_view = backend.createShaderView(cc::span{srv_sve}, cc::span{uav_sve}, {}, false);
            }

            arg::shader_table_record str_raygen;
            str_raygen.symbol = L"raygeneration";
            str_raygen.shader_arguments.push_back(shader_argument{resources.b_camdata_stacked, resources.raygen_shader_view, 0});

            arg::shader_table_record str_miss;
            str_miss.symbol = L"miss";

            arg::shader_table_record str_main_hit;
            str_main_hit.symbol = L"primary_hitgroup";

            table_sizes = backend.calculateShaderTableSize(cc::span{str_raygen}, cc::span{str_miss}, cc::span{str_main_hit});

            {
                resources.shader_table_raygen = backend.createUploadBuffer(table_sizes.ray_gen_stride_bytes * 1);
                std::byte* const st_map = backend.mapBuffer(resources.shader_table_raygen);
                backend.writeShaderTable(st_map, resources.rt_pso, table_sizes.ray_gen_stride_bytes, cc::span{str_raygen});
                backend.unmapBuffer(resources.shader_table_raygen);
            }

            {
                resources.shader_table_miss = backend.createUploadBuffer(table_sizes.miss_stride_bytes * 1);
                std::byte* const st_map = backend.mapBuffer(resources.shader_table_miss);
                backend.writeShaderTable(st_map, resources.rt_pso, table_sizes.miss_stride_bytes, cc::span{str_miss});
                backend.unmapBuffer(resources.shader_table_miss);
            }

            {
                resources.shader_table_hitgroups = backend.createUploadBuffer(table_sizes.hit_group_stride_bytes * 1);
                std::byte* const st_map = backend.mapBuffer(resources.shader_table_hitgroups);
                backend.writeShaderTable(st_map, resources.rt_pso, table_sizes.hit_group_stride_bytes, cc::span{str_main_hit});
                backend.unmapBuffer(resources.shader_table_hitgroups);
            }
        }
    };

    f_create_sized_resources();

    auto const on_resize_func = [&]() {
        backbuf_size = backend.getBackbufferSize(main_swapchain);

        f_free_sized_resources();
        f_create_sized_resources();
    };

    auto run_time = 0.f;
    inc::da::Timer timer;

    std::byte* mem_cmdlist = static_cast<std::byte*>(std::malloc(1024u * 10));
    CC_DEFER { std::free(mem_cmdlist); };

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

        if (window.clearPendingResize())
        {
            if (!window.isMinimized())
                backend.onResize(main_swapchain, window.getSize());
        }

        if (!window.isMinimized())
        {
            auto const frametime = timer.elapsedSeconds();
            timer.restart();
            run_time += frametime;

            if (backend.clearPendingResize(main_swapchain))
                on_resize_func();

            backbuf_index = cc::wrapped_increment(backbuf_index, 3u);
            camera.update_default_inputs(window, input, frametime);
            camera_data.fill_data(backbuf_size, camera.physical.position, camera.physical.forward, 0);

            auto* const map = backend.mapBuffer(resources.b_camdata_stacked, resources.camdata_stacked_offset * backbuf_index,
                                                resources.camdata_stacked_offset * (backbuf_index + 1));
            std::memcpy(map, &camera_data, sizeof(camera_data));
            backend.unmapBuffer(resources.b_camdata_stacked, resources.camdata_stacked_offset * backbuf_index,
                                resources.camdata_stacked_offset * (backbuf_index + 1));

            inc::imgui_new_frame(window.getSdlWindow());

            {
                command_stream_writer writer(mem_cmdlist, 1024u * 10);

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(resources.rt_write_texture, resource_state::unordered_access, shader_stage::ray_gen);

                    writer.add_command(tcmd);
                }

                {
                    cmd::dispatch_rays dcmd;
                    dcmd.pso = resources.rt_pso;
                    dcmd.table_raygen = resources.shader_table_raygen;
                    dcmd.table_miss = resources.shader_table_miss;
                    dcmd.table_hitgroups = resources.shader_table_hitgroups;
                    dcmd.width = backbuf_size.width;
                    dcmd.height = backbuf_size.height;
                    dcmd.depth = 1;

                    writer.add_command(dcmd);
                }

                auto const backbuffer = backend.acquireBackbuffer(main_swapchain);

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(resources.rt_write_texture, resource_state::copy_src);
                    tcmd.add(backbuffer, resource_state::copy_dest);
                    writer.add_command(tcmd);
                }
                {
                    cmd::copy_texture ccmd;
                    ccmd.init_symmetric(resources.rt_write_texture, backbuffer, backbuf_size.width, backbuf_size.height, 0);
                    writer.add_command(ccmd);
                }

                {
                    if (ImGui::Begin("Raytracing Demo"))
                    {
                        ImGui::Text("Frametime: %.2f ms", frametime * 1000.f);
                        ImGui::Text("backbuffer %u / %u", backbuf_index, 3);
                        ImGui::Text("cam pos: %.2f %.2f %.2f", double(camera.physical.position.x), double(camera.physical.position.y),
                                    double(camera.physical.position.z));
                        ImGui::Text("cam fwd: %.2f %.2f %.2f", double(camera.physical.forward.x), double(camera.physical.forward.y),
                                    double(camera.physical.forward.z));
                    }

                    ImGui::End();

                    ImGui::Render();
                    auto* const drawdata = ImGui::GetDrawData();
                    auto const commandsize = ImGui_ImplPHI_GetDrawDataCommandSize(drawdata);

                    cmd::transition_resources tcmd;
                    tcmd.add(backbuffer, resource_state::render_target, shader_stage::pixel);
                    writer.add_command(tcmd);

                    cmd::begin_render_pass bcmd;
                    bcmd.viewport = backbuf_size;
                    bcmd.add_backbuffer_rt(backbuffer, false);
                    writer.add_command(bcmd);

                    ImGui_ImplPHI_RenderDrawData(drawdata, {writer.buffer_head(), commandsize});
                    writer.advance_cursor(commandsize);

                    writer.add_command(cmd::end_render_pass{});
                }

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(backbuffer, resource_state::present);
                    writer.add_command(tcmd);
                }

                auto const cmdl = backend.recordCommandList(writer.buffer(), writer.size());
                backend.submit(cc::span{cmdl});
            }


            // present
            backend.present(main_swapchain);
        }
    }

    backend.flushGPU();
    shutdown_imgui();

    window.destroy();
    backend.destroy();
}
