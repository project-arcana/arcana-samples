#pragma once

#include <phantasm-hardware-interface/Backend.hh>
#include <phantasm-hardware-interface/detail/unique_buffer.hh>

#include <arcana-incubator/asset-loading/image_loader.hh>

namespace pr_test
{
void copy_and_gen_mip_data_to_texture(phi::command_stream_writer& writer,
                                      phi::handle::resource upload_buffer,
                                      std::byte* upload_buffer_map,
                                      phi::handle::resource dest_texture,
                                      phi::format format,
                                      inc::assets::image_size const& img_size,
                                      inc::assets::image_data& img_data,
                                      bool use_d3d12_per_row_alingment);

phi::detail::unique_buffer get_shader_binary(const char* name, ...);
}
