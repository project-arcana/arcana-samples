#include "texture_util.hh"

#include <cstdarg>
#include <cstdio>

#include <clean-core/utility.hh>
#include <clean-core/macros.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-renderer/backend/command_stream.hh>
#include <phantasm-renderer/backend/detail/byte_util.hh>
#include <phantasm-renderer/backend/detail/format_size.hh>

pr::backend::detail::unique_buffer pr_test::get_shader_binary(const char* name, ...)
{
    char name_formatted[1024];
    {
        va_list args;
        va_start(args, name);
#ifdef CC_COMPILER_MSVC
        ::vsprintf_s(name_formatted, 1024, name, args);
#else
        std::vsnprintf(name_formatted, 1024, name, args);
#endif
        va_end(args);
    }

    return pr::backend::detail::unique_buffer::create_from_binary_file(name_formatted);
}

void pr_test::copy_mipmaps_to_texture(pr::backend::command_stream_writer& writer,
                                      pr::backend::handle::resource upload_buffer,
                                      std::byte* upload_buffer_map,
                                      pr::backend::handle::resource dest_texture,
                                      pr::backend::format format,
                                      const inc::assets::image_size& img_size,
                                      inc::assets::image_data& img_data,
                                      bool use_d3d12_per_row_alingment,
                                      bool limit_to_first_mip)
{
    using namespace pr::backend;
    auto const bytes_per_pixel = detail::pr_format_size_bytes(format);

    CC_RUNTIME_ASSERT(img_size.array_size == 1 && "array upload unimplemented");
    cmd::copy_buffer_to_texture command;
    command.source = upload_buffer;
    command.destination = dest_texture;
    command.texture_format = format;
    command.dest_mip_size = limit_to_first_mip ? 1 : img_size.num_mipmaps;

    auto accumulated_offset_bytes = 0u;

    for (auto a = 0u; a < img_size.array_size; ++a)
    {
        command.dest_array_index = a;

        // copy all the mip slices into the offsets specified by the footprint structure
        //
        for (auto mip = 0u; mip < command.dest_mip_size; ++mip)
        {
            auto const mip_width = cc::max(unsigned(tg::floor(img_size.width / tg::pow(2.f, float(mip)))), 1u);
            auto const mip_height = cc::max(unsigned(tg::floor(img_size.height / tg::pow(2.f, float(mip)))), 1u);

            command.dest_width = mip_width;
            command.dest_height = mip_height;
            command.source_offset = accumulated_offset_bytes;
            command.dest_mip_index = mip;

            writer.add_command(command);

            auto const mip_row_size_bytes = bytes_per_pixel * command.dest_width;
            auto mip_row_stride_bytes = mip_row_size_bytes;

            if (use_d3d12_per_row_alingment)
                // MIP maps are 256-byte aligned per row in d3d12
                mip_row_stride_bytes = mem::align_up(mip_row_stride_bytes, 256);

            auto const mip_offset_bytes = mip_row_stride_bytes * command.dest_height;
            accumulated_offset_bytes += mip_offset_bytes;

            inc::assets::rowwise_copy(img_data, upload_buffer_map + command.source_offset, mip_row_stride_bytes, mip_row_size_bytes, command.dest_height);

            // if applicable, write the next mip level
            if (mip + 1 < command.dest_mip_size)
                inc::assets::write_mipmap(img_data, command.dest_width, command.dest_height);
        }
    }
}

unsigned pr_test::get_mipmap_upload_size(pr::backend::format format, const inc::assets::image_size& img_size, bool no_mips)
{
    using namespace pr::backend;
    auto const bytes_per_pixel = detail::pr_format_size_bytes(format);
    auto res_bytes = 0u;

    for (auto a = 0u; a < img_size.array_size; ++a)
    {
        auto const num_mips = no_mips ? 1 : img_size.num_mipmaps;
        for (auto mip = 0u; mip < num_mips; ++mip)
        {
            auto const mip_width = cc::max(unsigned(tg::floor(img_size.width / tg::pow(2.f, float(mip)))), 1u);
            auto const mip_height = cc::max(unsigned(tg::floor(img_size.height / tg::pow(2.f, float(mip)))), 1u);

            auto const row_pitch = mem::align_up(bytes_per_pixel * mip_width, 256);
            auto const custom_offset = row_pitch * mip_height;
            res_bytes += custom_offset;
        }
    }

    return res_bytes;
}


pr::backend::format pr_test::get_texture_format(bool hdr, int num_channels)
{
    using pf = pr::backend::format;
    if (hdr)
    {
        switch (num_channels)
        {
        case 4:
        default:
            return pf::rgba16f;
        case 2:
            return pf::rg16f;
        case 1:
            return pf::r16f;
        }
    }
    else
    {
        switch (num_channels)
        {
        case 4:
        default:
            return pf::rgba8un;
        case 2:
            return pf::rg8u;
        case 1:
            return pf::r8u;
        }
    }
    CC_RUNTIME_ASSERT(false && "invalid channel amount");
}
