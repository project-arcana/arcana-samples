#pragma once

#include <phantasm-hardware-interface/detail/unique_buffer.hh>

namespace phi_test
{
phi::detail::unique_buffer get_shader_binary(char const* name, char const* ending);
}
