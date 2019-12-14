#include "mip_generation.hh"

#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>
#include <clean-core/utility.hh>

#include <phantasm-renderer/backend/command_stream.hh>

#include "texture_util.hh"

using namespace pr::backend;

void pr_test::mip_generation_resources::initialize(pr::backend::Backend& backend, const char* shader_ending, bool align_rows)
{
    upload_buffers.reserve(500);
    mip_shader_views.reserve(2500);
    align_mip_rows = align_rows;
    this->backend = &backend;

    cc::capped_vector<arg::shader_argument_shape, 1> shader_payload;
    {
        arg::shader_argument_shape shape;
        shape.has_cb = false;
        shape.num_srvs = 1;
        shape.num_uavs = 1;
        shape.num_samplers = 0;
        shader_payload.push_back(shape);
    }

    auto const sb_mipgen = get_shader_binary("res/pr/liveness_sample/shader/bin/mipgen.%s", shader_ending);
    auto const sb_mipgen_gamma = get_shader_binary("res/pr/liveness_sample/shader/bin/mipgen_gamma.%s", shader_ending);
    auto const sb_mipgen_array = get_shader_binary("res/pr/liveness_sample/shader/bin/mipgen_array.%s", shader_ending);
    CC_RUNTIME_ASSERT(sb_mipgen.is_valid() && sb_mipgen_gamma.is_valid() && sb_mipgen_array.is_valid() && "failed to load shaders");

    pso_mipgen = backend.createComputePipelineState(shader_payload, arg::shader_stage{sb_mipgen.get(), sb_mipgen.size(), shader_domain::compute});
    pso_mipgen_gamma
        = backend.createComputePipelineState(shader_payload, arg::shader_stage{sb_mipgen_gamma.get(), sb_mipgen_gamma.size(), shader_domain::compute});
    pso_mipgen_array
        = backend.createComputePipelineState(shader_payload, arg::shader_stage{sb_mipgen_array.get(), sb_mipgen_array.size(), shader_domain::compute});
}

handle::resource pr_test::mip_generation_resources::load_texture(command_stream_writer& writer, const char* path, bool include_mipmaps, bool apply_gamma, int num_channels, bool hdr)
{
    inc::assets::image_size img_size;
    auto img_data = inc::assets::load_image(path, img_size, num_channels, hdr);
    CC_DEFER { inc::assets::free(img_data); };

    CC_RUNTIME_ASSERT(inc::assets::is_valid(img_data) && "failed to load texture");

    auto const format = pr_test::get_texture_format(hdr, num_channels);
    auto const res_handle
        = backend->createTexture(format, img_size.width, img_size.height, include_mipmaps ? img_size.num_mipmaps : 1, texture_dimension::t2d, 1, true);

    auto const upbuff_handle = backend->createMappedBuffer(pr_test::get_mipmap_upload_size(format, img_size, true));
    upload_buffers.push_back(upbuff_handle);

    {
        cmd::transition_resources transition_cmd;
        transition_cmd.add(res_handle, resource_state::copy_dest);
        writer.add_command(transition_cmd);
    }

    pr_test::copy_data_to_texture(writer, upbuff_handle, backend->getMappedMemory(upbuff_handle), res_handle, format, img_size, img_data, align_mip_rows);

    if (include_mipmaps)
        generate_mips(writer, res_handle, img_size, apply_gamma, format);

    return res_handle;
}

void pr_test::mip_generation_resources::generate_mips(command_stream_writer& writer, handle::resource resource, const inc::assets::image_size& size, bool apply_gamma, format pf)
{
    constexpr auto max_array_size = 16u;

    handle::pipeline_state matching_pso;
    if (size.array_size > 1)
    {
        matching_pso = pso_mipgen_array;
    }
    else
    {
        matching_pso = apply_gamma ? pso_mipgen_gamma : pso_mipgen;
    }

    auto const record_barriers = [&writer](cc::span<cmd::transition_image_slices::slice_transition_info const> slice_barriers) {
        cmd::transition_image_slices tcmd;

        for (auto const& ti : slice_barriers)
        {
            if (tcmd.transitions.size() == limits::max_resource_transitions)
            {
                writer.add_command(tcmd);
                tcmd.transitions.clear();
            }

            tcmd.transitions.push_back(ti);
        }

        if (!tcmd.transitions.empty())
            writer.add_command(tcmd);
    };


    cmd::transition_resources starting_tcmd;
    starting_tcmd.add(resource, resource_state::shader_resource);
    writer.add_command(starting_tcmd);


    for (auto level = 1u, levelWidth = size.width / 2, levelHeight = size.height / 2; level < size.num_mipmaps; ++level, levelWidth /= 2, levelHeight /= 2)
    {
        shader_view_element sve;
        sve.init_as_tex2d(resource, pf);
        sve.texture_info.mip_start = level - 1;
        sve.texture_info.mip_size = 1;
        if (size.array_size > 1)
        {
            sve.dimension = shader_view_dimension::texture2d_array;
            sve.texture_info.array_size = size.array_size;
        }

        shader_view_element sve_uav = sve;
        sve_uav.texture_info.mip_start = level;

        auto const sv = backend->createShaderView(cc::span{sve}, cc::span{sve_uav}, {}, true);
        mip_shader_views.push_back(sv);

        cc::capped_vector<cmd::transition_image_slices::slice_transition_info, max_array_size> pre_dispatch;
        cc::capped_vector<cmd::transition_image_slices::slice_transition_info, max_array_size> post_dispatch;

        for (auto arraySlice = 0u; arraySlice < size.array_size; ++arraySlice)
        {
            pre_dispatch.push_back(cmd::transition_image_slices::slice_transition_info{
                resource, resource_state::shader_resource, resource_state::unordered_access, int(level), int(arraySlice)});
            post_dispatch.push_back(cmd::transition_image_slices::slice_transition_info{
                resource, resource_state::unordered_access, resource_state::shader_resource, int(level), int(arraySlice)});
        }

        // record pre-dispatch barriers
        record_barriers(pre_dispatch);

        // record compute dispatch
        cmd::dispatch dcmd;
        dcmd.init(matching_pso, cc::max(1u, levelWidth / 8), cc::max(1u, levelHeight / 8), size.array_size);
        dcmd.add_shader_arg(handle::null_resource, 0, sv);
        writer.add_command(dcmd);

        // record post-dispatch barriers
        record_barriers(post_dispatch);
    }

    cmd::transition_resources closing_tcmd;
    closing_tcmd.add(resource, resource_state::shader_resource);
    writer.add_command(closing_tcmd);
}
