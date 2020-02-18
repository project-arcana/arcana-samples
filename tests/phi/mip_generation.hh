#pragma once

#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/Backend.hh>
#include <phantasm-hardware-interface/commands.hh>

#include "texture_util.hh"

namespace phi_test
{
struct texture_creation_resources
{
    void initialize(phi::Backend& backend);

    void free(phi::Backend& backend);

    phi::handle::resource load_texture(char const* path, phi::format format, bool include_mipmaps, bool apply_gamma = false);

    void finish_uploads() { flush_cmdstream(true, true); }

public:
    // IBL

    phi::handle::resource load_filtered_specular_map(char const* hdr_equirect_path);

    phi::handle::resource create_diffuse_irradiance_map(phi::handle::resource filtered_specular_map);

    phi::handle::resource create_brdf_lut(int width_height = 256);

private:
    void generate_mips(phi::handle::resource resource, inc::assets::image_size const& size, bool apply_gamma, phi::format pf);

    void flush_cmdstream(bool dispatch, bool stall);

private:
    phi::Backend* backend = nullptr;
    bool align_mip_rows;

    phi::handle::pipeline_state pso_mipgen;
    phi::handle::pipeline_state pso_mipgen_gamma;
    phi::handle::pipeline_state pso_mipgen_array;

    phi::handle::pipeline_state pso_equirect_to_cube;
    phi::handle::pipeline_state pso_specular_map_filter;
    phi::handle::pipeline_state pso_irradiance_map_gen;
    phi::handle::pipeline_state pso_brdf_lut_gen;


private:
    std::byte* commandstream_buffer = nullptr;
    phi::command_stream_writer cmd_writer;

    cc::vector<phi::handle::resource> resources_to_free;
    cc::vector<phi::handle::shader_view> shader_views_to_free;
    cc::vector<phi::handle::command_list> pending_cmd_lists;
};


}
