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

    if (!backend.gpuHasRaytracing())
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
    } resources;

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

                    tg::mat4 transform = tg::transpose(tg::mat4::identity);
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


    auto const on_resize_func = [&]() {};

    auto run_time = 0.f;
    auto log_time = 0.f;
    inc::da::Timer timer;
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
            continue;
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

            backend.acquireBackbuffer();

            // present
            backend.present();
        }
    }

    backend.flushGPU();
}
