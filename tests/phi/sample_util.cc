#include "sample_util.hh"

#include <cstdio>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>
#include <arcana-incubator/imgui/imgui.hh>
#include <arcana-incubator/imgui/imgui_impl_phi.hh>
#include <arcana-incubator/imgui/imgui_impl_sdl2.hh>

#include <phantasm-hardware-interface/Backend.hh>

phi::unique_buffer phi_test::get_shader_binary(const char* name, const char* ending)
{
    char name_formatted[1024];
    std::snprintf(name_formatted, sizeof(name_formatted), "res/phi/shader/bin/%s.%s", name, ending);
    auto res = phi::unique_buffer::create_from_binary_file(name_formatted);
    if (!res.is_valid())
    {
        std::fprintf(stderr, "[phi_test] failed to load shader from %s\n[phi_test]   shaders not compiled? run res/<..>/compile_shaders.bat/.sh\n\n", name_formatted);
    }
    return res;
}

void phi_test::initialize_imgui(inc::da::SDLWindow& window, phi::Backend& backend)
{
    inc::imgui_init(window.getSdlWindow(), &backend, 3, phi::format::bgra8un);
    window.setEventCallback(ImGui_ImplSDL2_ProcessEvent);
}

void phi_test::shutdown_imgui() { inc::imgui_shutdown(); }
