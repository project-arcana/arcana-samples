#pragma once

#include <phantasm-renderer/backend/Backend.hh>

#include <arcana-incubator/asset-loading/image_loader.hh>

namespace pr_test
{
void copy_mipmaps_to_texture(pr::backend::command_stream_writer& writer,
                             pr::backend::handle::resource upload_buffer,
                             std::byte* upload_buffer_map,
                             pr::backend::handle::resource dest_texture,
                             pr::backend::format format,
                             inc::assets::image_size const& img_size,
                             inc::assets::image_data& img_data,
                             bool use_d3d12_per_row_alingment,
                             bool limit_to_first_mip = false);


unsigned get_mipmap_upload_size(pr::backend::format format, inc::assets::image_size const& img_size);


pr::backend::format get_texture_format(bool hdr, unsigned num_channels);
}
