#pragma once

#include <clean-core/vector.hh>

#include <phantasm-renderer/backend/Backend.hh>
#include <phantasm-renderer/backend/command_stream.hh>

#include "texture_util.hh"

namespace pr_test
{
struct texture_creation_resources
{
    void initialize(pr::backend::Backend& backend, char const* shader_ending, bool align_rows);

    void free(pr::backend::Backend& backend);

    pr::backend::handle::resource load_texture(char const* path, pr::backend::format format, bool include_mipmaps, bool apply_gamma = false);

    pr::backend::handle::resource load_environment_map_from_equirect(char const* path);

    void finish_uploads() { flush_cmdstream(true); }

private:
    void generate_mips(pr::backend::handle::resource resource,
                       inc::assets::image_size const& size,
                       bool apply_gamma,
                       pr::backend::format pf);

    void flush_cmdstream(bool wait_gpu = false);

private:
    pr::backend::Backend* backend = nullptr;
    bool align_mip_rows;

    pr::backend::handle::pipeline_state pso_mipgen;
    pr::backend::handle::pipeline_state pso_mipgen_gamma;
    pr::backend::handle::pipeline_state pso_mipgen_array;

    pr::backend::handle::pipeline_state pso_equirect_to_cube;


private:
    std::byte* commandstream_buffer = nullptr;
    pr::backend::command_stream_writer cmd_writer;

    cc::vector<pr::backend::handle::resource> resources_to_free;
    cc::vector<pr::backend::handle::shader_view> shader_views_to_free;
    cc::vector<pr::backend::handle::command_list> pending_cmd_lists;
};


}
