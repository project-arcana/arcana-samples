#pragma once

#include <phantasm-renderer/backend/Backend.hh>
#include <phantasm-renderer/backend/detail/unique_buffer.hh>

#include <arcana-incubator/asset-loading/image_loader.hh>

namespace pr_test
{
void copy_and_gen_mip_data_to_texture(pr::backend::command_stream_writer& writer,
                                      pr::backend::handle::resource upload_buffer,
                                      std::byte* upload_buffer_map,
                                      pr::backend::handle::resource dest_texture,
                                      pr::backend::format format,
                                      inc::assets::image_size const& img_size,
                                      inc::assets::image_data& img_data,
                                      bool use_d3d12_per_row_alingment);

void copy_data_to_texture(pr::backend::command_stream_writer& writer,
                          pr::backend::handle::resource upload_buffer,
                          std::byte* upload_buffer_map,
                          pr::backend::handle::resource dest_texture,
                          pr::backend::format format,
                          inc::assets::image_size const& img_size,
                          const inc::assets::image_data& img_data,
                          bool use_d3d12_per_row_alingment);

unsigned get_mipmap_upload_size(pr::backend::format format, inc::assets::image_size const& img_size, bool no_mips = false);

pr::backend::detail::unique_buffer get_shader_binary(const char* name, ...);
}
