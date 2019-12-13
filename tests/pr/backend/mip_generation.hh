#pragma once

#include <clean-core/vector.hh>

#include <phantasm-renderer/backend/Backend.hh>

#include "texture_util.hh"

namespace pr_test
{
struct mip_generation_resources
{
    void initialize(pr::backend::Backend& backend, char const* shader_ending, bool align_rows);

    void free(pr::backend::Backend& backend)
    {
        backend.free_range(upload_buffers);
        backend.free(pso_mipgen);
        backend.free(pso_mipgen_gamma);
        backend.free(pso_mipgen_array);
    }

    pr::backend::handle::resource load_texture(pr::backend::command_stream_writer& writer, char const* path, bool apply_gamma = false, unsigned num_channels = 4, bool hdr = false);

private:
    void generate_mips(pr::backend::command_stream_writer& writer, pr::backend::handle::resource resource, inc::assets::image_size const& size, bool apply_gamma);

private:
    pr::backend::handle::pipeline_state pso_mipgen;
    pr::backend::handle::pipeline_state pso_mipgen_gamma;
    pr::backend::handle::pipeline_state pso_mipgen_array;
    bool align_mip_rows;
    pr::backend::Backend* backend;

    cc::vector<pr::backend::handle::resource> upload_buffers;
};


}
