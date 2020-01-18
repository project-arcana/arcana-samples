#include "sample.hh"

#include <cmath>

#include <rich-log/log.hh>

#include <clean-core/capped_array.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-renderer/backend/command_stream.hh>
#include <phantasm-renderer/backend/detail/unique_buffer.hh>

#include <arcana-incubator/asset-loading/image_loader.hh>
#include <arcana-incubator/asset-loading/mesh_loader.hh>
#include <arcana-incubator/device-abstraction/timer.hh>
#include <arcana-incubator/device-abstraction/window.hh>

#include "sample_scene.hh"
#include "sample_util.hh"
#include "texture_util.hh"


void pr_test::run_raytracing_sample(pr::backend::Backend& backend, sample_config const& sample_config, pr::backend::backend_config const& backend_config)
{
    using namespace pr::backend;

    inc::da::Window window;
    window.initialize(sample_config.window_title);
    backend.initialize(backend_config, {window.getNativeHandleA(), window.getNativeHandleB()});

    if (!backend.isRaytracingEnabled())
    {
        LOG(warning)("Current GPU has no raytracing capabilities");
        return;
    }

    struct resources_t
    {
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

    tg::isize2 backbuf_size = tg::isize2(50, 50);
    shader_table_sizes table_sizes;

    // Res setup
    {
        auto const buffer_size = 1024ull * 2;
        auto* const buffer = static_cast<std::byte*>(std::malloc(buffer_size));
        CC_DEFER { std::free(buffer); };
        command_stream_writer writer(buffer, buffer_size);

        // Mesh setup
        {
            writer.reset();
            handle::resource upload_buffer;
            {
                auto const mesh_data = pr_test::sample_mesh_binary ? inc::assets::load_binary_mesh(pr_test::sample_mesh_path)
                                                                   : inc::assets::load_obj_mesh(pr_test::sample_mesh_path);

                resources.num_indices = unsigned(mesh_data.indices.size());
                resources.num_vertices = unsigned(mesh_data.vertices.size());

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

            auto const meshupload_list = backend.recordCommandList(writer.buffer(), writer.size());

            backend.flushMappedMemory(upload_buffer);
            backend.submit(cc::span{meshupload_list});
            backend.flushGPU();
            backend.free(upload_buffer);
        }

        // AS / RT setup
        {
            writer.reset();
            {
                arg::blas_element blas_elem;
                blas_elem.is_opaque = true;
                blas_elem.index_buffer = resources.index_buffer;
                blas_elem.vertex_buffer = resources.vertex_buffer;
                blas_elem.num_indices = resources.num_indices;
                blas_elem.num_vertices = resources.num_vertices;

                constexpr unsigned num_instances = 1;

                resources.blas = backend.createBottomLevelAccelStruct(
                    cc::span{blas_elem}, accel_struct_build_flag_bits::prefer_fast_trace | accel_struct_build_flag_bits::allow_compaction, &resources.blas_native);
                resources.tlas = backend.createTopLevelAccelStruct(num_instances);


                cc::array<accel_struct_geometry_instance, num_instances> instance_data;

                for (auto i = 0u; i < num_instances; ++i)
                {
                    auto& inst = instance_data[i];
                    inst.mask = 0xFF;
                    inst.flags = accel_struct_instance_flag_bits::triangle_front_counterclockwise;
                    inst.instance_id = i;
                    inst.native_accel_struct_handle = resources.blas_native;
                    inst.instance_offset = i * 2;

                    tg::mat4 transform = tg::transpose(tg::scaling(tg::size3(.1f, .1f, .1f)));
                    std::memcpy(inst.transform, tg::data_ptr(transform), sizeof(inst.transform));
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
            shader_binaries.push_back(get_shader_binary("res/pr/liveness_sample/shader/bin/raytrace_lib.%s", sample_config.shader_ending));
            auto& main_lib = libraries.emplace_back();
            main_lib.binary = {shader_binaries.back().get(), shader_binaries.back().size()};
            main_lib.symbols = {L"raygeneration", L"miss", L"closesthit"};
        }

        cc::capped_vector<arg::raytracing_argument_association, 16> arg_assocs;

        {
            auto& raygen_assoc = arg_assocs.emplace_back();
            raygen_assoc.symbols = {L"raygeneration"};
            raygen_assoc.argument_shapes.push_back(arg::shader_argument_shape{1, 1, 0, false});
            raygen_assoc.has_root_constants = false;

            auto& closesthit_assoc = arg_assocs.emplace_back();
            closesthit_assoc.symbols = {L"closesthit"};
            closesthit_assoc.argument_shapes.push_back(arg::shader_argument_shape{1, 0, 0, false});
        }

        arg::raytracing_hit_group main_hit_group;
        main_hit_group.name = L"primary_hitgroup";
        main_hit_group.closest_hit_symbol = L"closesthit";

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
        resources.rt_write_texture
            = backend.createTexture(backend.getBackbufferFormat(), backbuf_size.width, backbuf_size.height, 1, texture_dimension::t2d, 1, true);

        // Shader table setup
        {
            {
                shader_view_element uav_sve;
                uav_sve.init_as_tex2d(resources.rt_write_texture, backend.getBackbufferFormat());

                shader_view_element srv_sve;
                srv_sve.init_as_accel_struct(backend.getAccelStructBuffer(resources.tlas));

                resources.raygen_shader_view = backend.createShaderView(cc::span{srv_sve}, cc::span{uav_sve}, {}, false);
            }

            arg::shader_table_record str_raygen;
            str_raygen.symbol = L"raygeneration";
            str_raygen.shader_arguments.push_back(shader_argument{handle::null_resource, resources.raygen_shader_view, 0});

            arg::shader_table_record str_miss;
            str_miss.symbol = L"miss";

            arg::shader_table_record str_main_hit;
            str_main_hit.symbol = L"primary_hitgroup";

            table_sizes = backend.calculateShaderTableSize(cc::span{str_raygen}, cc::span{str_miss}, cc::span{str_main_hit});

            {
                resources.shader_table_raygen = backend.createMappedBuffer(table_sizes.ray_gen_stride_bytes * 1);
                std::byte* const st_map = backend.getMappedMemory(resources.shader_table_raygen);
                backend.writeShaderTable(st_map, resources.rt_pso, table_sizes.ray_gen_stride_bytes, cc::span{str_raygen});
            }

            {
                resources.shader_table_miss = backend.createMappedBuffer(table_sizes.miss_stride_bytes * 1);
                std::byte* const st_map = backend.getMappedMemory(resources.shader_table_miss);
                backend.writeShaderTable(st_map, resources.rt_pso, table_sizes.miss_stride_bytes, cc::span{str_miss});
            }

            {
                resources.shader_table_hitgroups = backend.createMappedBuffer(table_sizes.hit_group_stride_bytes * 1);
                std::byte* const st_map = backend.getMappedMemory(resources.shader_table_hitgroups);
                backend.writeShaderTable(st_map, resources.rt_pso, table_sizes.hit_group_stride_bytes, cc::span{str_main_hit});
            }
        }
    };

    f_create_sized_resources();

    auto const on_resize_func = [&]() {
        backbuf_size = backend.getBackbufferSize();

        f_free_sized_resources();
        f_create_sized_resources();
    };

    auto run_time = 0.f;
    auto log_time = 0.f;
    inc::da::Timer timer;

    std::byte* mem_cmdlist = static_cast<std::byte*>(std::malloc(1024u * 10));
    CC_DEFER { std::free(mem_cmdlist); };

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
            log_time += frametime;

            if (log_time >= 1750.f)
            {
                log_time = 0.f;
                LOG(info)("frametime: %fms", static_cast<double>(frametime));
            }

            if (backend.clearPendingResize())
                on_resize_func();

            {
                command_stream_writer writer(mem_cmdlist, 1024u * 10);

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(resources.rt_write_texture, resource_state::unordered_access, shader_domain_bits::ray_gen);

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

                auto const backbuffer = backend.acquireBackbuffer();

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
                    cmd::transition_resources tcmd;
                    tcmd.add(backbuffer, resource_state::present);
                    writer.add_command(tcmd);
                }

                auto const cmdl = backend.recordCommandList(writer.buffer(), writer.size());
                backend.submit(cc::span{cmdl});
            }


            // present
            backend.present();
        }
    }

    backend.flushGPU();
}
