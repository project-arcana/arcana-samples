#pragma once

#include <phantasm-hardware-interface/detail/unique_buffer.hh>

namespace inc
{
class ImGuiPhantasmImpl;
namespace da
{
class SDLWindow;
}
}

namespace phi
{
class Backend;
}

namespace phi_test
{
phi::detail::unique_buffer get_shader_binary(char const* name, char const* ending);

void initialize_imgui(inc::ImGuiPhantasmImpl& impl, inc::da::SDLWindow& window, phi::Backend& backend);
void shutdown_imgui(inc::ImGuiPhantasmImpl& impl);
}
