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
#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/container/unique_buffer.hh>
#include <phantasm-hardware-interface/util.hh>
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
    conf.validation = validation_level::on_extended_dred;
    //    conf.native_features |= conf.native_feature_d3d12_break_on_warn;
    backend.initialize(conf);

    if (!backend.isRaytracingEnabled())
    {
        if (backend_config.enable_raytracing)
            LOG_WARN("current GPU has no raytracing capabilities");
        else
            LOG_WARN("raytracing was explicitly disabled");
        return;
    }

    // window init
    inc::da::initialize();
    inc::da::SDLWindow window;
    window.initialize(sample_config.window_title);
    initialize_imgui(window, backend);

    // main swapchain creation
    phi::handle::swapchain const main_swapchain = backend.createSwapchain({window.getSdlWindow()}, window.getSize(), present_mode::unsynced_allow_tearing);
    // unsigned const msc_num_backbuffers = backend.getNumBackbuffers(main_swapchain);
    phi::format const msc_backbuf_format = backend.getBackbufferFormat(main_swapchain);


    inc::da::input_manager input;
    input.initialize();
    inc::da::smooth_fps_cam camera;
    inc::pre::dmr::camera_gpudata camera_data;
    camera.setup_default_inputs(input);
    camera.target.position = {33, 18, 16};
    camera.target.forward = tg::normalize(tg::vec3{-.44f, -.52f, -.73f});
    camera.physical = camera.target;

    struct resources_t
    {
        handle::resource b_camdata_stacked;
        unsigned camdata_stacked_offset = 0;

        handle::resource vertex_buffer;
        handle::resource index_buffer;
        unsigned num_indices = 0;
        unsigned num_vertices = 0;

        handle::accel_struct blas;
        uint64_t blas_native = 0;
        handle::accel_struct tlas;
        handle::resource tlas_instance_buffer;

        handle::pipeline_state rt_pso;

        handle::resource rt_write_texture;

        handle::shader_view sv_ray_gen;
        handle::shader_view sv_mesh_buffers;

        handle::resource shader_table;
    } resources;

    unsigned backbuf_index = 0;
    tg::isize2 backbuf_size = tg::isize2(50, 50);
    shader_table_strides table_sizes;
    util::shader_table_offsets table_offsets;

    // Res setup
    handle::resource init_upload_buffer;
    {
        auto const buffer_size = 1024ull * 2;
        auto* const buffer = static_cast<std::byte*>(std::malloc(buffer_size));
        CC_DEFER { std::free(buffer); };
        command_stream_writer writer(buffer, buffer_size);

        // camdata buffer
        {
            unsigned const size_camdata_256 = phi::util::align_up<unsigned>(sizeof(inc::pre::dmr::camera_gpudata), 256);
            resources.b_camdata_stacked = backend.createUploadBuffer(size_camdata_256 * 3, size_camdata_256);
            resources.camdata_stacked_offset = size_camdata_256;
        }

        // Mesh setup
        {
            writer.reset();
            {
                auto const mesh_data = phi_test::sample_mesh_binary ? inc::assets::load_binary_mesh(phi_test::sample_mesh_path)
                                                                    : inc::assets::load_obj_mesh(phi_test::sample_mesh_path);

                resources.num_indices = unsigned(mesh_data.indices.size());
                resources.num_vertices = unsigned(mesh_data.vertices.size());

                cc::vector<uint16_t> indices16(resources.num_indices);

                for (auto i = 0u; i < resources.num_indices; ++i)
                {
                    indices16[i] = uint16_t(mesh_data.indices[i]);
                }

                auto const vert_size = mesh_data.vertices.size_bytes();
                auto const ind_size = indices16.size_bytes();

                resources.vertex_buffer = backend.createBuffer(vert_size, sizeof(inc::assets::simple_vertex));
                resources.index_buffer = backend.createBuffer(ind_size, sizeof(uint16_t));

                {
                    resource_view mesh_buffer_rvs[2];
                    mesh_buffer_rvs[0].init_as_structured_buffer(resources.index_buffer, resources.num_indices, sizeof(uint16_t));
                    mesh_buffer_rvs[1].init_as_structured_buffer(resources.vertex_buffer, resources.num_vertices, sizeof(inc::assets::simple_vertex));
                    resources.sv_mesh_buffers = backend.createShaderView(mesh_buffer_rvs, {}, {}, false);
                }

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(resources.vertex_buffer, resource_state::copy_dest);
                    tcmd.add(resources.index_buffer, resource_state::copy_dest);
                    writer.add_command(tcmd);
                }

                init_upload_buffer = backend.createUploadBuffer(vert_size + ind_size);
                {
                    std::byte* const upload_mapped = backend.mapBuffer(init_upload_buffer);

                    std::memcpy(upload_mapped, mesh_data.vertices.data(), vert_size);
                    std::memcpy(upload_mapped + vert_size, indices16.data(), ind_size);

                    backend.unmapBuffer(init_upload_buffer);
                }

                writer.add_command(cmd::copy_buffer{resources.vertex_buffer, 0, init_upload_buffer, 0, vert_size});
                writer.add_command(cmd::copy_buffer{resources.index_buffer, 0, init_upload_buffer, vert_size, ind_size});

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(resources.vertex_buffer, resource_state::vertex_buffer);
                    tcmd.add(resources.index_buffer, resource_state::index_buffer);
                    writer.add_command(tcmd);
                }
            }

            auto const meshupload_list = backend.recordCommandList(writer.buffer(), writer.size());

            backend.submit(cc::span{meshupload_list});
        }

        // AS / RT setup
        {
            constexpr unsigned num_blas_elements = 1;

            constexpr unsigned instance_cube_edge_length = 4;
            constexpr auto instance_cube_dimensions = tg::vec<4, size_t>{instance_cube_edge_length, instance_cube_edge_length, instance_cube_edge_length, 1};

            constexpr unsigned num_tlas_instances = instance_cube_edge_length * instance_cube_edge_length * instance_cube_edge_length;

            // Bottom Level Accel Struct (BLAS) - Geometry elements
            {
                arg::blas_element blas_elements[num_blas_elements];

                for (auto i = 0u; i < num_blas_elements; ++i)
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
            }

            // Top Level Accel Struct (TLAS) - BLAS instances
            {
                accel_struct_instance instance_data[num_tlas_instances];

                resources.tlas = backend.createTopLevelAccelStruct(num_tlas_instances, accel_struct_build_flags::prefer_fast_trace);
                resources.tlas_instance_buffer = backend.createUploadBuffer(sizeof(instance_data), sizeof(instance_data[0]));

                for (auto i = 0u; i < num_tlas_instances; ++i)
                {
                    auto& inst = instance_data[i];
                    inst.instance_id = i;
                    inst.visibility_mask = 0xFF;
                    inst.hit_group_index = i % 3;
                    inst.flags = accel_struct_instance_flags::triangle_front_counterclockwise;
                    inst.native_bottom_level_as_handle = resources.blas_native;

                    auto const pos = phi_test::dimensional_index(instance_cube_dimensions, i) * 20;

                    tg::mat4 const transform
                        = tg::transpose(tg::translation<float>(pos.x, pos.y, pos.z) * tg::rotation_y(0_deg) /* * tg::scaling(.1f, .1f, .1f)*/);
                    std::memcpy(inst.transposed_transform, tg::data_ptr(transform), sizeof(inst.transposed_transform));
                }


                {
                    auto const instances_span = cc::span<accel_struct_instance>(instance_data);

                    std::byte* const map = backend.mapBuffer(resources.tlas_instance_buffer);
                    std::memcpy(map, instances_span.data(), instances_span.size_bytes());
                    backend.unmapBuffer(resources.tlas_instance_buffer);
                }
            }

            // GPU timeline - build BLAS and TLAS
            {
                writer.reset();
                cmd::update_bottom_level bcmd;
                bcmd.dest = resources.blas;
                writer.add_command(bcmd);

                cmd::update_top_level tcmd;
                tcmd.num_instances = num_tlas_instances;
                tcmd.source_buffer_instances = resources.tlas_instance_buffer;
                tcmd.dest_accel_struct = resources.tlas;
                writer.add_command(tcmd);
            }

            auto const accelstruct_cmdlist = backend.recordCommandList(writer.buffer(), writer.size());
            backend.submit(cc::span{accelstruct_cmdlist});
        }
    }

    // PSO setup
    {
        cc::capped_vector<phi::unique_buffer, 16> shader_binaries;
        cc::capped_vector<arg::raytracing_shader_library, 16> libraries;

        {
            shader_binaries.push_back(get_shader_binary("raytrace_lib", sample_config.shader_ending));
            CC_RUNTIME_ASSERT(shader_binaries.back().is_valid() && "failed to load raytracing_lib shader");
            auto& main_lib = libraries.emplace_back();
            main_lib.binary = {shader_binaries.back().get(), shader_binaries.back().size()};
            main_lib.shader_exports = {{shader_stage::ray_gen, "EPrimaryRayGen"},
                                       {shader_stage::ray_miss, "EMiss"},
                                       {shader_stage::ray_closest_hit, "EClosestHitFlatColor"},
                                       {shader_stage::ray_closest_hit, "EBarycentricClosestHit"},
                                       {shader_stage::ray_closest_hit, "EClosestHitErrorState"}};
        }

        cc::capped_vector<arg::raytracing_argument_association, 16> arg_assocs;

        {
            auto& raygen_assoc = arg_assocs.emplace_back();
            raygen_assoc.target_type = raygen_assoc.e_target_identifiable_shader;
            raygen_assoc.target_indices = {0};                                            // EPrimaryRayGen
            raygen_assoc.argument_shapes.push_back(arg::shader_arg_shape{1, 1, 0, true}); // t: accelstruct, u: output tex
            raygen_assoc.has_root_constants = false;

            auto& hitgroup_assoc = arg_assocs.emplace_back();
            hitgroup_assoc.target_type = raygen_assoc.e_target_hitgroup;
            hitgroup_assoc.target_indices = {0, 1, 2};                                       // all hitgroups
            hitgroup_assoc.argument_shapes.push_back(arg::shader_arg_shape{1, 1, 0, false}); // t: accelstruct
            hitgroup_assoc.argument_shapes.push_back(arg::shader_arg_shape{2, 0, 0, false}); // t: mesh vertex and index buffer
        }

        arg::raytracing_hit_group hit_groups[3];

        hit_groups[0].name = "hitgroup0";
        hit_groups[0].closest_hit_export_index = 2; // = "EBarycentricClosestHit";

        hit_groups[1].name = "hitgroup1";
        hit_groups[1].closest_hit_export_index = 3; // = "EClosestHitFlatColor";

        hit_groups[2].name = "hitgroup2";
        hit_groups[2].closest_hit_export_index = 4; //= "EClosestHitErrorState";

        arg::raytracing_pipeline_state_desc desc;
        desc.libraries = libraries;
        desc.argument_associations = arg_assocs;
        desc.hit_groups = hit_groups;
        desc.max_recursion = 8;
        desc.max_payload_size_bytes = sizeof(float[4]) + sizeof(uint32_t); // float4: RGB + Distance, uint: current recursion
        desc.max_attribute_size_bytes = sizeof(float[2]);                  // Barycentrics, builtin Triangles

        resources.rt_pso = backend.createRaytracingPipelineState(desc);
    }

    auto const f_free_sized_resources = [&] {
        backend.free(resources.rt_write_texture);
        backend.free(resources.sv_ray_gen);
        backend.free(resources.shader_table);
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

                resources.sv_ray_gen = backend.createShaderView(cc::span{srv_sve}, cc::span{uav_sve}, {}, false);
            }

            arg::shader_table_record str_raygen;
            str_raygen.set_shader(0); // str_raygen.symbol = "raygeneration";
            str_raygen.add_shader_arg(resources.b_camdata_stacked, 0, resources.sv_ray_gen);

            arg::shader_table_record str_miss;
            str_miss.set_shader(1); // str_miss.symbol = "miss";

            arg::shader_table_record str_hitgroups[3];

            str_hitgroups[0].set_hitgroup(0);
            str_hitgroups[0].add_shader_arg(handle::null_resource, 0, resources.sv_ray_gen);
            str_hitgroups[0].add_shader_arg(handle::null_resource, 0, resources.sv_mesh_buffers);

            str_hitgroups[1].set_hitgroup(1);
            str_hitgroups[1].add_shader_arg(handle::null_resource, 0, resources.sv_ray_gen);
            str_hitgroups[1].add_shader_arg(handle::null_resource, 0, resources.sv_mesh_buffers);

            str_hitgroups[2].set_hitgroup(2);
            str_hitgroups[2].add_shader_arg(handle::null_resource, 0, resources.sv_ray_gen);
            str_hitgroups[2].add_shader_arg(handle::null_resource, 0, resources.sv_mesh_buffers);

            table_sizes = backend.calculateShaderTableStrides(str_raygen, cc::span{str_miss}, str_hitgroups);

            table_offsets.init(table_sizes, 3, 1, 1, 0);

            {
                resources.shader_table = backend.createUploadBuffer(table_offsets.total_size);
                std::byte* const st_map = backend.mapBuffer(resources.shader_table);


                for (auto stack_i = 0u; stack_i < 3u; ++stack_i)
                {
                    // set the CBV offset for this ray_generation record's argument
                    str_raygen.shader_arguments[0].constant_buffer_offset = resources.camdata_stacked_offset * stack_i;

                    // write it to the mapped shader table at this stack's position
                    backend.writeShaderTable(st_map + table_offsets.get_ray_gen_offset(stack_i), resources.rt_pso, 0, cc::span{str_raygen});
                }

                backend.writeShaderTable(st_map + table_offsets.get_miss_offset(0), resources.rt_pso, table_sizes.stride_miss, cc::span{str_miss});
                backend.writeShaderTable(st_map + table_offsets.get_hitgroup_offset(0), resources.rt_pso, table_sizes.stride_hit_group, str_hitgroups);

                backend.unmapBuffer(resources.shader_table);
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

    backend.flushGPU();
    backend.free(init_upload_buffer);

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

            auto const current_camdata_offset = resources.camdata_stacked_offset * backbuf_index;
            auto const current_camdata_range = resources.camdata_stacked_offset * (backbuf_index + 1);

            auto* const map = backend.mapBuffer(resources.b_camdata_stacked, current_camdata_offset, current_camdata_range);
            std::memcpy(map + current_camdata_offset, &camera_data, sizeof(camera_data));
            backend.unmapBuffer(resources.b_camdata_stacked, current_camdata_offset, current_camdata_range);

            inc::imgui_new_frame(window.getSdlWindow());

            {
                auto const backbuffer = backend.acquireBackbuffer(main_swapchain);
                command_stream_writer writer(mem_cmdlist, 1024u * 10);

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(resources.rt_write_texture, resource_state::unordered_access, shader_stage::ray_gen);

                    writer.add_command(tcmd);
                }

                {
                    cmd::dispatch_rays dcmd;
                    dcmd.pso = resources.rt_pso;
                    dcmd.width = backbuf_size.width;
                    dcmd.height = backbuf_size.height;
                    dcmd.depth = 1;
                    dcmd.set_strides(table_sizes);
                    dcmd.set_single_buffer(resources.shader_table, false);
                    dcmd.set_offsets(table_offsets.get_ray_gen_offset(backbuf_index), table_offsets.get_miss_offset(0), table_offsets.get_hitgroup_offset(0), 0);
                    //                    dcmd.set_single_shader_table(resources.shader_table, table_sizes);
                    //                    dcmd.set_zero_sizes();

                    writer.add_command(dcmd);
                    // LOG_INFO("ray gen offset: {} (bb {})", dcmd.table_ray_generation.offset_bytes, backbuf_index);
                }

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

                    {
                        cmd::transition_resources tcmd;
                        tcmd.add(backbuffer, resource_state::render_target);
                        writer.add_command(tcmd);
                    }

                    {
                        cmd::begin_render_pass bcmd;
                        bcmd.viewport = backbuf_size;
                        bcmd.add_backbuffer_rt(backbuffer, false);
                        writer.add_command(bcmd);
                    }

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

                handle::command_list submits[] = {cmdl};
                backend.submit(submits);
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
