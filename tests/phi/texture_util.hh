#pragma once

#include <phantasm-hardware-interface/Backend.hh>
#include <phantasm-hardware-interface/detail/unique_buffer.hh>

#include <arcana-incubator/asset-loading/image_loader.hh>

namespace phi_test
{
phi::detail::unique_buffer get_shader_binary(char const* name, char const* ending);
}
