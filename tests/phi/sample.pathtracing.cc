#include "sample.hh"

#include <cmath>
#include <cstdio>


#include <clean-core/capped_array.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

#include <rich-log/log.hh>

#include <reflector/macros.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/commands.hh>
#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/container/unique_buffer.hh>
#include <phantasm-hardware-interface/common/log_util.hh>
#include <phantasm-hardware-interface/util.hh>
#include <phantasm-hardware-interface/window_handle.hh>

#include <phantasm-renderer/reflection/gpu_buffer_alignment.hh>

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
template <class T>
struct alignas(16) f4pad
{
    T val;
};

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

struct pathtrace_composite_data
{
    unsigned num_cumulative_samples;
    unsigned num_input_samples;
    tg::usize2 viewport_size;
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
    char _pad0[12];
    f4pad<pathtrace_light_type> type[ARC_PHI_TEST_MAX_NUM_LIGHTS];   // 3 pad
    f4pad<tg::vec3> position[ARC_PHI_TEST_MAX_NUM_LIGHTS];           // 1 pad
    f4pad<tg::vec3> normal[ARC_PHI_TEST_MAX_NUM_LIGHTS];             // 1 pad
    f4pad<tg::vec3> color[ARC_PHI_TEST_MAX_NUM_LIGHTS];              // 1 pad
    f4pad<tg::vec3> dPdu[ARC_PHI_TEST_MAX_NUM_LIGHTS];               // 1 pad
    f4pad<tg::vec3> dPdv[ARC_PHI_TEST_MAX_NUM_LIGHTS];               // 1 pad
    f4pad<tg::vec3> dimensions[ARC_PHI_TEST_MAX_NUM_LIGHTS];         // 1 pad
    f4pad<float> attenuation[ARC_PHI_TEST_MAX_NUM_LIGHTS];           // 3 pad
    f4pad<float> rectLightBarnCosAngle[ARC_PHI_TEST_MAX_NUM_LIGHTS]; // 3 pad
    f4pad<float> rectLightBarnLength[ARC_PHI_TEST_MAX_NUM_LIGHTS];   // 3 pad

    tg::vec4 skyLightSHData[7];
};

REFL_INTROSPECT_FUNC(pathtrace_cbv)
{
    REFL_INTROSPECT_FIELD(proj);
    REFL_INTROSPECT_FIELD(proj_inv);
    REFL_INTROSPECT_FIELD(view);
    REFL_INTROSPECT_FIELD(view_inv);
    REFL_INTROSPECT_FIELD(vp);
    REFL_INTROSPECT_FIELD(vp_inv);
    REFL_INTROSPECT_FIELD(frame_index);
    REFL_INTROSPECT_FIELD(num_samples_per_pixel);
    REFL_INTROSPECT_FIELD(max_bounces);
    REFL_INTROSPECT_FIELD(fov_radians);
    REFL_INTROSPECT_FIELD(cam_to_pixel_spread_angle_radians);
}

REFL_INTROSPECT_FUNC(pathtrace_lightdata_soa)
{
    REFL_INTROSPECT_FIELD4(numLights, type, position, normal);
    REFL_INTROSPECT_FIELD4(color, dPdu, dPdv, dimensions);
    REFL_INTROSPECT_FIELD4(attenuation, rectLightBarnCosAngle, rectLightBarnLength, skyLightSHData);
}
}

void phi_test::run_pathtracing_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& backend_config)
{
    CC_ASSERT(pr::test_gpu_buffer_alignment<pathtrace_cbv>(nullptr, true) && "fatal");
    CC_ASSERT(pr::test_gpu_buffer_alignment<pathtrace_lightdata_soa>(nullptr, true) && "fatal");

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
        handle::resource b_camdata_stacked;
        unsigned camdata_stacked_offset = 0;
        handle::resource b_lightdata;

        handle::resource vertex_buffer;
        handle::resource index_buffer;
        unsigned num_indices = 0;
        unsigned num_vertices = 0;

        handle::accel_struct blas;
        uint64_t blas_native = 0;
        handle::accel_struct tlas;
        handle::resource tlas_instance_buffer;

        handle::pipeline_state rt_pso;
        handle::pipeline_state pso_tonemap;

        handle::resource rt_write_texture;
        handle::resource t_current_num_samples;

        handle::resource t_cumulative_irradiance_a;
        handle::resource t_cumulative_irradiance_b;
        handle::resource t_cumulative_num_samples_a;
        handle::resource t_cumulative_num_samples_b;

        handle::shader_view sv_ray_gen;
        handle::shader_view sv_mesh_buffers;
        handle::shader_view sv_composite_a;
        handle::shader_view sv_composite_b;

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
            unsigned const size_camdata_256 = phi::util::align_up<unsigned>(sizeof(pathtrace_cbv), 256);
            resources.b_camdata_stacked = backend.createUploadBuffer(size_camdata_256 * 3, size_camdata_256, "stacked camera/framedata");
            resources.camdata_stacked_offset = size_camdata_256;
        }

        // lightdata buffer
        {
            resources.b_lightdata = backend.createUploadBuffer(sizeof(pathtrace_lightdata_soa), 0, "lightdata SOA");

            pathtrace_lightdata_soa* const map = (pathtrace_lightdata_soa*)backend.mapBuffer(resources.b_lightdata);

            std::memset(map, 0, sizeof(pathtrace_lightdata_soa));

            auto f_add_light = [map](pathtrace_light_type type, tg::vec3 pos, tg::vec3 normal, tg::vec3 color, float attenuation, tg::vec3 dimensions) {
                auto const index = map->numLights++;
                map->type[index].val = type;
                map->position[index].val = pos;
                map->normal[index].val = normal;
                map->color[index].val = color;
                map->attenuation[index].val = attenuation;
                map->dimensions[index].val = dimensions;
            };

            auto f_add_envlight = [&](tg::vec3 color) { //
                f_add_light(pathtrace_light_type::EnvLight, {0, 0, 0}, {0, 0, 0}, color, 0.f, {0, 0, 0});
            };

            auto f_add_pointlight = [&](tg::vec3 pos, tg::vec3 color, float radius, float attenuation) {
                f_add_light(pathtrace_light_type::PointLight, pos, {0, 0, 0}, color, attenuation, {0, 0, radius});
            };


            f_add_envlight(tg::vec3(1, 1, 1) * .5f);

            f_add_pointlight({28, 30, 33}, {50, 0, 0}, 3.f, 25.f);
            f_add_pointlight({11, 73, 52}, {0, 25, 25}, 3.f, 25.f);
            f_add_pointlight({49, 11, 9}, {25, 15, 0}, 3.f, 25.f);

            SHVecColor ambient_sh;
            ambient_sh.add_ambient(tg::color3(1, 1, 1) * 2.f);
            ambient_sh.add_radiance(tg::color3(.9f, .5f, 0), 5.f, tg::normalize(tg::vec3(1, 1, 1)));
            ambient_sh.add_radiance(tg::color3(0, 0, 1), 5.f, tg::normalize(tg::vec3(-1, 1, 1)));
            preprocess_spherical_harmonics(ambient_sh, map->skyLightSHData);

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
                    inst.hit_group_index = 0;
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

    // Composite PSO setup
    {
        auto const vs = get_shader_binary("fullscreen_vs", sample_config.shader_ending);
        auto const ps = get_shader_binary("pathtrace_composite", sample_config.shader_ending);
        CC_RUNTIME_ASSERT(vs.is_valid() && ps.is_valid() && "failed to load shaders");


        arg::graphics_pipeline_state_desc desc;
        desc.framebuffer.add_render_target(msc_backbuf_format); // target0: backbuffer / output
        desc.framebuffer.add_render_target(write_tex_format);   // target1: new cumulative
        desc.framebuffer.add_render_target(format::r32u);       // target2: new cumulative num samples

        desc.shader_binaries
            = {arg::graphics_shader{{vs.get(), vs.size()}, shader_stage::vertex}, arg::graphics_shader{{ps.get(), ps.size()}, shader_stage::pixel}};

        // current and cumulative irradiance and num samples, SRVs
        desc.shader_arg_shapes = {arg::shader_arg_shape(4, 0, 0)};

        desc.has_root_constants = true;

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
            raygen_assoc.set_target_identifiable();
            raygen_assoc.target_indices = {0};                                            // EPrimaryRayGen
            raygen_assoc.argument_shapes.push_back(arg::shader_arg_shape{1, 2, 0, true}); // t: accelstruct; u: output tex, output num samples; b: cam data
            raygen_assoc.argument_shapes.push_back(arg::shader_arg_shape{0, 0, 0, true}); // b: light data
        }
        {
            auto& miss_assoc = arg_assocs.emplace_back();
            miss_assoc.set_target_identifiable();
            miss_assoc.target_indices = {1};
        }
        {
            auto& hitgroup_assoc = arg_assocs.emplace_back();
            hitgroup_assoc.set_target_hitgroup();
            hitgroup_assoc.target_indices = {0, 1};                                          // all hitgroups
            hitgroup_assoc.argument_shapes.push_back(arg::shader_arg_shape{0, 0, 0, false}); // t: accelstruct
            hitgroup_assoc.argument_shapes.push_back(arg::shader_arg_shape{2, 0, 0, false}); // t: mesh vertex and index buffer
        }

        arg::raytracing_hit_group hit_groups[2];

        hit_groups[0].name = "SHADING_MODEL_0_HGM";
        hit_groups[0].closest_hit_export_index = 2; // = "ECH0Material";

        hit_groups[1].name = "SHADING_MODEL_0_HGS";
        hit_groups[1].closest_hit_export_index = 3; // = "ECH0Shadow";

        arg::raytracing_pipeline_state_desc desc;
        desc.libraries = libraries;
        desc.argument_associations = arg_assocs;
        desc.hit_groups = hit_groups;
        desc.max_recursion = 16;
        desc.max_payload_size_bytes = 166;
        desc.max_attribute_size_bytes = sizeof(float[2]); // barycentrics, the builtin attribute for triangles

        resources.rt_pso = backend.createRaytracingPipelineState(desc);
    }

    auto f_free_sized_resources = [&] {
        handle::resource res_to_free[]
            = {resources.rt_write_texture,          resources.t_current_num_samples,     resources.shader_table,
               resources.t_cumulative_irradiance_a, resources.t_cumulative_irradiance_b, resources.t_cumulative_num_samples_a,
               resources.t_cumulative_num_samples_b};
        backend.freeRange(res_to_free);

        handle::shader_view sv_to_free[] = {resources.sv_ray_gen, resources.sv_composite_a, resources.sv_composite_b};
        backend.freeRange(sv_to_free);
    };

    auto f_create_sized_resources = [&] {
        // textures
        resources.rt_write_texture = backend.createTexture(write_tex_format, backbuf_size, 1, texture_dimension::t2d, 1, true, "pathtrace current irradiance");
        resources.t_current_num_samples = backend.createTexture(format::r32u, backbuf_size, 1, texture_dimension::t2d, 1, true, "pathtrace current num samples");
        resources.t_cumulative_irradiance_a = backend.createRenderTarget(write_tex_format, backbuf_size);
        resources.t_cumulative_irradiance_b = backend.createRenderTarget(write_tex_format, backbuf_size);
        resources.t_cumulative_num_samples_a = backend.createRenderTarget(format::r32u, backbuf_size);
        resources.t_cumulative_num_samples_b = backend.createRenderTarget(format::r32u, backbuf_size);

        // SVs
        {
            resource_view srvs[4];
            srvs[0].init_as_tex2d(resources.rt_write_texture, write_tex_format);
            srvs[1].init_as_tex2d(resources.t_cumulative_irradiance_a, write_tex_format);
            srvs[2].init_as_tex2d(resources.t_current_num_samples, format::r32u);
            srvs[3].init_as_tex2d(resources.t_cumulative_num_samples_a, format::r32u);

            resources.sv_composite_a = backend.createShaderView(srvs, {}, {});

            srvs[1].resource = resources.t_cumulative_irradiance_b;
            srvs[3].resource = resources.t_cumulative_num_samples_b;

            resources.sv_composite_b = backend.createShaderView(srvs, {}, {});
        }

        {
            resource_view uavs[2];
            uavs[0].init_as_tex2d(resources.rt_write_texture, write_tex_format);
            uavs[1].init_as_tex2d(resources.t_current_num_samples, format::r32u);

            resource_view srvs[1];
            srvs[0].init_as_accel_struct(backend.getAccelStructBuffer(resources.tlas));

            resources.sv_ray_gen = backend.createShaderView(srvs, uavs, {}, false);
        }

        // Shader table setup
        {
            arg::shader_table_record str_raygen;
            str_raygen.set_shader(0);                                                        // str_raygen.symbol = "raygeneration";
            str_raygen.add_shader_arg(resources.b_camdata_stacked, 0, resources.sv_ray_gen); // the offset here will be adjusted later per stack
            str_raygen.add_shader_arg(resources.b_lightdata);

            arg::shader_table_record str_miss;
            str_miss.set_shader(1); // str_miss.symbol = "miss";

            arg::shader_table_record str_hitgroups[2];

            str_hitgroups[0].set_hitgroup(0);
            str_hitgroups[0].add_shader_arg(handle::null_resource, 0, handle::null_shader_view);
            str_hitgroups[0].add_shader_arg(handle::null_resource, 0, resources.sv_mesh_buffers);

            str_hitgroups[1].set_hitgroup(1);
            str_hitgroups[1].add_shader_arg(handle::null_resource, 0, handle::null_shader_view);
            str_hitgroups[1].add_shader_arg(handle::null_resource, 0, resources.sv_mesh_buffers);

            table_sizes = backend.calculateShaderTableStrides(str_raygen, cc::span{str_miss}, str_hitgroups);

            //            LOG_INFO("----------------");
            //            LOG_INFO("----------------");
            LOG_INFO("table sizes: raygen {}, hitgroup {}, miss {}", table_sizes.size_ray_gen, table_sizes.size_hit_group, table_sizes.size_miss);
            //            LOG_INFO("----------------");
            //            LOG_INFO("----------------");

            table_offsets.init(table_sizes, 3, 1, 1, 0);

            {
                resources.shader_table = backend.createBuffer(table_offsets.total_size, 0, resource_heap::upload, false, "main shader table");
                std::byte* const st_map = backend.mapBuffer(resources.shader_table);

                [[maybe_unused]] auto f_log_section = [st_map, limit = table_offsets.total_size](char const* name, size_t offset, size_t size,
                                                                                                 unsigned stack_i = 0, unsigned num_stacks = 1) -> void {
                    LOG_INFO("------ {} shader table (stack {}/{}) ------", name, stack_i + 1, num_stacks);
                    LOG_INFO("------ map offset: {}, size: {}, far: {} (limit: {}) ------", offset, size, offset + size, limit);

                    log::dump_hex(st_map + offset, size);

                    LOG_INFO("------ end of {} memory ------", name);
                };

                for (auto stack_i = 0u; stack_i < 3u; ++stack_i)
                {
                    // set the CBV offset for this ray_generation record's argument
                    str_raygen.shader_arguments[0].constant_buffer_offset = resources.camdata_stacked_offset * stack_i;

                    // write it to the mapped shader table at this stack's position
                    backend.writeShaderTable(st_map + table_offsets.get_ray_gen_offset(stack_i), resources.rt_pso, 0, cc::span{str_raygen});

                    // f_log_section("ray generation", table_offsets.get_ray_gen_offset(stack_i), table_offsets.strides.size_ray_gen, stack_i, 3);
                }

                backend.writeShaderTable(st_map + table_offsets.get_miss_offset(0), resources.rt_pso, table_sizes.stride_miss, cc::span{str_miss});
                // f_log_section("miss", table_offsets.get_miss_offset(0), table_sizes.stride_miss);

                CC_ASSERT(table_offsets.get_hitgroup_offset(0) + table_sizes.stride_hit_group * cc::span(str_hitgroups).size() <= table_offsets.total_size
                          && "last table entry OOB");

                backend.writeShaderTable(st_map + table_offsets.get_hitgroup_offset(0), resources.rt_pso, table_sizes.stride_hit_group, str_hitgroups);
                // f_log_section("hitgroups", table_offsets.get_hitgroup_offset(0), table_sizes.stride_hit_group);

                //                log::dump_hex(st_map, table_offsets.total_size);

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

    pathtrace_composite_data composite_data = {};
    bool is_accumulation_enabled = true;

    bool is_frame_atob = true;

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
            is_frame_atob = !is_frame_atob;

            composite_data.num_input_samples = cbv_data.num_samples_per_pixel;
            composite_data.num_cumulative_samples += composite_data.num_input_samples;

            if (backend.clearPendingResize(main_swapchain))
                on_resize_func();

            backbuf_index = cc::wrapped_increment(backbuf_index, 3u);

            bool const did_cam_change = camera.update_default_inputs(window, input, frametime);

            if (did_cam_change || !is_accumulation_enabled)
            {
                composite_data.num_cumulative_samples = 0;
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
                    tcmd.add(resources.t_current_num_samples, resource_state::unordered_access, shader_stage::ray_gen);
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

                auto const t_cumulative_irradiance_src = is_frame_atob ? resources.t_cumulative_irradiance_a : resources.t_cumulative_irradiance_b;
                auto const t_cumulative_irradiance_dst = is_frame_atob ? resources.t_cumulative_irradiance_b : resources.t_cumulative_irradiance_a;

                auto const t_cumulative_num_samples_src = is_frame_atob ? resources.t_cumulative_num_samples_a : resources.t_cumulative_num_samples_b;
                auto const t_cumulative_num_samples_dst = is_frame_atob ? resources.t_cumulative_num_samples_b : resources.t_cumulative_num_samples_a;

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(resources.rt_write_texture, resource_state::shader_resource, shader_stage::pixel);
                    tcmd.add(t_cumulative_irradiance_src, resource_state::shader_resource, shader_stage::pixel);
                    tcmd.add(t_cumulative_irradiance_dst, resource_state::render_target);
                    tcmd.add(backbuffer, resource_state::render_target);
                    writer.add_command(tcmd);
                }

                {
                    cmd::transition_resources tcmd;
                    tcmd.add(t_cumulative_num_samples_src, resource_state::shader_resource, shader_stage::pixel);
                    tcmd.add(resources.t_current_num_samples, resource_state::shader_resource, shader_stage::pixel);
                    tcmd.add(t_cumulative_num_samples_dst, resource_state::render_target);
                    writer.add_command(tcmd);
                }

                {
                    cmd::begin_render_pass bcmd;
                    bcmd.viewport = backbuf_size;
                    bcmd.add_backbuffer_rt(backbuffer);
                    bcmd.add_2d_rt(t_cumulative_irradiance_dst, write_tex_format);
                    bcmd.add_2d_rt(t_cumulative_num_samples_dst, format::r32u);
                    writer.add_command(bcmd);
                }

                {
                    auto const sv_source = is_frame_atob ? resources.sv_composite_a : resources.sv_composite_b;
                    composite_data.viewport_size = tg::usize2{backbuf_size};

                    cmd::draw dcmd;
                    dcmd.init(resources.pso_tonemap, 3);
                    dcmd.add_shader_arg(handle::null_resource, 0, sv_source);
                    dcmd.write_root_constants(composite_data);
                    writer.add_command(dcmd);
                }

                {
                    // restart render pass with just the single backbuffer RT (no clear)
                    writer.add_command(cmd::end_render_pass{});

                    cmd::begin_render_pass bcmd;
                    bcmd.viewport = backbuf_size;
                    bcmd.add_backbuffer_rt(backbuffer, false);
                    writer.add_command(bcmd);
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
                        ImGui::Text("Accum. samples: %u, #spp: %u", composite_data.num_cumulative_samples, composite_data.num_input_samples);

                        ImGui::Checkbox("Enable Accumulation", &is_accumulation_enabled);
                        ImGui::SliderInt("Samples per Pixel", &cbv_data.num_samples_per_pixel, 0, 15);
                        ImGui::SliderInt("Max Bounces", &cbv_data.max_bounces, 1, 15);
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
