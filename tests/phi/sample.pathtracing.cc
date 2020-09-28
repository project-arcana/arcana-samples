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

namespace
{
template <size_t N1, size_t N2 = 1, size_t N3 = 1, size_t N4 = 1>
constexpr inline tg::vec<4, size_t> dimensional_index(size_t linear)
{
    auto const i1 = linear % N1;
    auto const i2 = ((linear - i1) / N1) % N2;
    auto const i3 = ((linear - i2 * N1 - i1) / (N1 * N2)) % N3;
    auto const i4 = ((linear - i3 * N2 * N1 - i2 * N1 - i1) / (N1 * N2 * N3)) % N4;
    return {i1, i2, i3, i4};
}

struct pathtrace_cbv
{
    tg::mat4 proj;
    tg::mat4 proj_inv;
    tg::mat4 view;
    tg::mat4 view_inv;
    tg::mat4 vp;
    tg::mat4 vp_inv;
    int frame_index;
    int num_samples_per_pixel;
    int max_bounces;
    float fov_radians;
    float cam_to_pixel_spread_angle_radians;
};

#define ARC_PHI_TEST_MAX_NUM_LIGHTS 256

enum class pathtrace_light_type : unsigned
{
    EnvLight = 0,
    PointLight,
    DirectionalLight,
    RectLight,
    SpotLight
};

struct pathtrace_lightdata_soa
{
    unsigned numLights;
    pathtrace_light_type type[ARC_PHI_TEST_MAX_NUM_LIGHTS];
    tg::vec3 position[ARC_PHI_TEST_MAX_NUM_LIGHTS];
    tg::vec3 normal[ARC_PHI_TEST_MAX_NUM_LIGHTS];
    tg::vec3 color[ARC_PHI_TEST_MAX_NUM_LIGHTS];
    tg::vec3 dPdu[ARC_PHI_TEST_MAX_NUM_LIGHTS];
    tg::vec3 dPdv[ARC_PHI_TEST_MAX_NUM_LIGHTS];
    tg::vec3 dimensions[ARC_PHI_TEST_MAX_NUM_LIGHTS];
    float attenuation[ARC_PHI_TEST_MAX_NUM_LIGHTS];
    float rectLightBarnCosAngle[ARC_PHI_TEST_MAX_NUM_LIGHTS];
    float rectLightBarnLength[ARC_PHI_TEST_MAX_NUM_LIGHTS];
};
}

void phi_test::run_pathtracing_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& backend_config)
{
    using namespace phi;
    // backend init

    // auto conf = backend_config;
    // conf.validation = validation_level::on_extended_dred;
    //    conf.native_features |= conf.native_feature_d3d12_break_on_warn;
    backend.initialize(backend_config);

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
    phi::format const write_tex_format = phi::format::b10g11r11uf;


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
        handle::resource b_camdata_stacked = handle::null_resource;
        unsigned camdata_stacked_offset = 0;
        handle::resource b_lightdata;

        handle::resource vertex_buffer = handle::null_resource;
        handle::resource index_buffer = handle::null_resource;
        unsigned num_indices = 0;
        unsigned num_vertices = 0;

        handle::accel_struct blas = handle::null_accel_struct;
        uint64_t blas_native = 0;
        handle::accel_struct tlas = handle::null_accel_struct;
        handle::resource tlas_instance_buffer = handle::null_resource;

        handle::pipeline_state rt_pso = handle::null_pipeline_state;
        handle::pipeline_state pso_tonemap;

        handle::resource rt_write_texture = handle::null_resource;

        handle::shader_view sv_ray_gen = handle::null_shader_view;
        handle::shader_view sv_mesh_buffers = handle::null_shader_view;
        handle::shader_view sv_ray_trace_result;

        handle::resource shader_table = handle::null_resource;
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
            unsigned const size_camdata_256 = phi::util::align_up<unsigned>(sizeof(pathtrace_cbv), 256);
            resources.b_camdata_stacked = backend.createUploadBuffer(size_camdata_256 * 3, size_camdata_256);
            resources.camdata_stacked_offset = size_camdata_256;
        }

        // lightdata buffer
        {
            resources.b_lightdata = backend.createUploadBuffer(sizeof(pathtrace_lightdata_soa));

            pathtrace_lightdata_soa* const map = (pathtrace_lightdata_soa*)backend.mapBuffer(resources.b_lightdata);

            std::memset(map, 0, sizeof(pathtrace_lightdata_soa));

            auto f_add_light = [map](pathtrace_light_type type, tg::vec3 pos, tg::vec3 normal, tg::vec3 color) {
                auto const index = map->numLights++;
                map->type[index] = type;
                map->position[index] = pos;
                map->normal[index] = normal;
                map->color[index] = color;
            };

            f_add_light(pathtrace_light_type::EnvLight, {}, {}, {});
            f_add_light(pathtrace_light_type::PointLight, {-2, 1, 0}, {0, 1, 0}, {1, 0, 0});
            f_add_light(pathtrace_light_type::PointLight, {0, 1, 0}, {0, -1, 0}, {0, 1, 0});
            f_add_light(pathtrace_light_type::PointLight, {2, 1, 0}, {0, 1, 0}, {0, 0, 1});

            backend.unmapBuffer(resources.b_lightdata);
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
                    inst.hit_group_index = 0;
                    inst.flags = accel_struct_instance_flags::triangle_front_counterclockwise;
                    inst.native_bottom_level_as_handle = resources.blas_native;

                    auto const pos = dimensional_index<instance_cube_edge_length, instance_cube_edge_length, instance_cube_edge_length>(i) * 20;

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

    // Output PSO setup
    {
        auto const vs = get_shader_binary("fullscreen_vs", sample_config.shader_ending);
        auto const ps = get_shader_binary("postprocess_ps", sample_config.shader_ending);
        CC_RUNTIME_ASSERT(vs.is_valid() && ps.is_valid() && "failed to load shaders");


        arg::graphics_pipeline_state_desc desc;
        desc.config.cull = cull_mode::front;

        desc.framebuffer.add_render_target(msc_backbuf_format);

        desc.shader_binaries
            = {arg::graphics_shader{{vs.get(), vs.size()}, shader_stage::vertex}, arg::graphics_shader{{ps.get(), ps.size()}, shader_stage::pixel}};

        // Argument 0, blit target SRV + sampler
        desc.shader_arg_shapes = {arg::shader_arg_shape(1, 0, 1)};

        resources.pso_tonemap = backend.createPipelineState(desc);
    }

    // RT PSO setup
    {
        cc::capped_vector<phi::unique_buffer, 16> shader_binaries;
        cc::capped_vector<arg::raytracing_shader_library, 16> libraries;

        {
            shader_binaries.push_back(get_shader_binary("pathtrace_lib", sample_config.shader_ending));
            CC_RUNTIME_ASSERT(shader_binaries.back().is_valid() && "failed to load pathtrace_lib shader");
            auto& main_lib = libraries.emplace_back();
            main_lib.binary = {shader_binaries.back().get(), shader_binaries.back().size()};
            main_lib.shader_exports = {{shader_stage::ray_gen, "EPrimaryRayGen"},
                                       {shader_stage::ray_miss, "EMiss"},

                                       {shader_stage::ray_closest_hit, "ECH0Material"},
                                       {shader_stage::ray_closest_hit, "ECH0Shadow"}};
        }

        cc::capped_vector<arg::raytracing_argument_association, 16> arg_assocs;

        {
            auto& raygen_assoc = arg_assocs.emplace_back();
            raygen_assoc.library_index = 0;
            raygen_assoc.export_indices = {0};                                            // EPrimaryRayGen
            raygen_assoc.argument_shapes.push_back(arg::shader_arg_shape{1, 1, 0, true}); // t: accelstruct; u: output tex
            raygen_assoc.argument_shapes.push_back(arg::shader_arg_shape{0, 0, 0, true}); // b: light data
            raygen_assoc.has_root_constants = false;
        }
        {
            auto& hitgroup_assoc = arg_assocs.emplace_back();
            hitgroup_assoc.library_index = 0;
            hitgroup_assoc.export_indices = {2, 3};                                          // all closest hit exports
            hitgroup_assoc.argument_shapes.push_back(arg::shader_arg_shape{1, 1, 0, true});  // t: accelstruct
            hitgroup_assoc.argument_shapes.push_back(arg::shader_arg_shape{2, 0, 0, false}); // t: mesh vertex and index buffer
        }

        arg::raytracing_hit_group hit_groups[2];

        hit_groups[0].name = "SHADING_MODEL_0_HGM";
        hit_groups[0].closest_hit_name = "ECH0Material";

        hit_groups[1].name = "SHADING_MODEL_0_HGS";
        hit_groups[1].closest_hit_name = "ECH0Shadow";

        arg::raytracing_pipeline_state_desc desc;
        desc.libraries = libraries;
        desc.argument_associations = arg_assocs;
        desc.hit_groups = hit_groups;
        desc.max_recursion = 16;
        desc.max_payload_size_bytes = 166;
        desc.max_attribute_size_bytes = sizeof(float[2]); // Barycentrics, builtin Triangles

        resources.rt_pso = backend.createRaytracingPipelineState(desc);
    }

    auto f_free_sized_resources = [&] {
        backend.free(resources.rt_write_texture);
        backend.free(resources.sv_ray_gen);
        backend.free(resources.shader_table);
        backend.free(resources.sv_ray_trace_result);
    };

    auto f_create_sized_resources = [&] {
        // textures
        resources.rt_write_texture = backend.createTexture(write_tex_format, backbuf_size, 1, texture_dimension::t2d, 1, true);

        // SVs
        {
            resource_view srvs[1];
            srvs[0].init_as_tex2d(resources.rt_write_texture, write_tex_format);

            sampler_config samplers[1];
            samplers[0].init_default(sampler_filter::min_mag_mip_point);

            resources.sv_ray_trace_result = backend.createShaderView(srvs, {}, samplers);
        }

        {
            resource_view uavs[1];
            uavs[0].init_as_tex2d(resources.rt_write_texture, write_tex_format);

            resource_view srvs[1];
            srvs[0].init_as_accel_struct(backend.getAccelStructBuffer(resources.tlas));

            sampler_config samplers[1];
            samplers[0].init_default(sampler_filter::min_mag_mip_linear);

            resources.sv_ray_gen = backend.createShaderView(srvs, uavs, samplers, false);
        }

        // Shader table setup
        {
            arg::shader_table_record str_raygen;
            str_raygen.set_shader(0); // str_raygen.symbol = "raygeneration";
            str_raygen.add_shader_arg(resources.b_camdata_stacked, 0, resources.sv_ray_gen);
            str_raygen.add_shader_arg(resources.b_lightdata);

            arg::shader_table_record str_miss;
            str_miss.set_shader(1); // str_miss.symbol = "miss";

            arg::shader_table_record str_hitgroups[2];

            str_hitgroups[0].set_hitgroup(0);
            str_hitgroups[0].add_shader_arg(handle::null_resource, 0, resources.sv_ray_gen);
            str_hitgroups[0].add_shader_arg(handle::null_resource, 0, resources.sv_mesh_buffers);

            str_hitgroups[1].set_hitgroup(1);
            str_hitgroups[1].add_shader_arg(handle::null_resource, 0, resources.sv_ray_gen);
            str_hitgroups[1].add_shader_arg(handle::null_resource, 0, resources.sv_mesh_buffers);

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

    auto cam_fov = tg::degree(60);

    pathtrace_cbv cbv_data = {};
    cbv_data.max_bounces = 5;
    cbv_data.num_samples_per_pixel = 1;

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
            ++cbv_data.frame_index;

            if (backend.clearPendingResize(main_swapchain))
                on_resize_func();

            backbuf_index = cc::wrapped_increment(backbuf_index, 3u);

            bool const did_cam_change = camera.update_default_inputs(window, input, frametime);

            if (did_cam_change)
            {
                cbv_data.frame_index = 0;
            }

            camera_data.fill_data(backbuf_size, camera.physical.position, camera.physical.forward, 0, cam_fov);

            cbv_data.proj = camera_data.proj;
            cbv_data.proj_inv = camera_data.proj_inv;
            cbv_data.view = camera_data.view;
            cbv_data.view_inv = camera_data.view_inv;
            cbv_data.vp = camera_data.clean_vp;
            cbv_data.vp_inv = camera_data.clean_vp_inv;
            cbv_data.fov_radians = cam_fov.radians();
            cbv_data.cam_to_pixel_spread_angle_radians = tg::atan((2.f * tg::tan(cam_fov * .5f)) / backbuf_size.width).radians();

            auto const current_camdata_offset = resources.camdata_stacked_offset * backbuf_index;
            auto const current_camdata_range = resources.camdata_stacked_offset * (backbuf_index + 1);

            auto* const map = backend.mapBuffer(resources.b_camdata_stacked, current_camdata_offset, current_camdata_range);
            std::memcpy(map + current_camdata_offset, &cbv_data, sizeof(cbv_data));
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
                    tcmd.add(resources.rt_write_texture, resource_state::shader_resource, shader_stage::pixel);
                    tcmd.add(backbuffer, resource_state::render_target);
                    writer.add_command(tcmd);
                }

                {
                    cmd::begin_render_pass bcmd;
                    bcmd.viewport = backbuf_size;
                    bcmd.add_backbuffer_rt(backbuffer);
                    writer.add_command(bcmd);
                }

                {
                    cmd::draw dcmd;
                    dcmd.init(resources.pso_tonemap, 3);
                    dcmd.add_shader_arg(handle::null_resource, 0, resources.sv_ray_trace_result);
                    writer.add_command(dcmd);
                }

                {
                    if (ImGui::Begin("Unbiased Pathtracing Demo"))
                    {
                        ImGui::Text("Frametime: %.2f ms", frametime * 1000.f);
                        ImGui::Text("backbuffer %u / %u", backbuf_index, 3);
                        ImGui::Text("cam pos: %.2f %.2f %.2f", double(camera.physical.position.x), double(camera.physical.position.y),
                                    double(camera.physical.position.z));
                        ImGui::Text("cam fwd: %.2f %.2f %.2f", double(camera.physical.forward.x), double(camera.physical.forward.y),
                                    double(camera.physical.forward.z));

                        ImGui::Separator();

                        ImGui::Text("Frame %d", cbv_data.frame_index);
                        ImGui::SliderInt("Samples per Pixel", &cbv_data.num_samples_per_pixel, 1, 10);
                        ImGui::SliderInt("Max Bounces", &cbv_data.max_bounces, 1, 10);
                    }

                    ImGui::End();

                    ImGui::Render();
                    auto* const drawdata = ImGui::GetDrawData();
                    auto const commandsize = ImGui_ImplPHI_GetDrawDataCommandSize(drawdata);

                    ImGui_ImplPHI_RenderDrawData(drawdata, {writer.buffer_head(), commandsize});
                    writer.advance_cursor(commandsize);
                }

                writer.add_command(cmd::end_render_pass{});

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
