#include "sample_util.hh"

#include <cstdio>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>
#include <arcana-incubator/imgui/imgui_impl_pr.hh>
#include <arcana-incubator/imgui/imgui_impl_sdl2.hh>

#include <phantasm-hardware-interface/Backend.hh>

phi::detail::unique_buffer phi_test::get_shader_binary(const char* name, const char* ending)
{
    char name_formatted[1024];
    std::snprintf(name_formatted, sizeof(name_formatted), "res/phi/shader/bin/%s.%s", name, ending);
    auto res = phi::detail::unique_buffer::create_from_binary_file(name_formatted);
    if (!res.is_valid())
    {
        std::fprintf(stderr, "[phi_test] failed to load shader from %s\n[phi_test]   shaders not compiled? run res/<..>/compile_shaders.bat/.sh\n\n", name_formatted);
    }
    return res;
}

void phi_test::initialize_imgui(inc::ImGuiPhantasmImpl& impl, inc::da::SDLWindow& window, phi::Backend& backend)
{
    char const* const shader_ending = backend.getBackendType() == phi::backend_type::d3d12 ? "dxil" : "spv";

    ImGui::SetCurrentContext(ImGui::CreateContext(nullptr));
    ImGui_ImplSDL2_Init(window.getSdlWindow());
    window.setEventCallback(ImGui_ImplSDL2_ProcessEvent);

    {
        auto const ps_bin = get_shader_binary("imgui_ps", shader_ending);
        auto const vs_bin = get_shader_binary("imgui_vs", shader_ending);
        impl.initialize(&backend, ps_bin.get(), ps_bin.size(), vs_bin.get(), vs_bin.size());
    }
}

void phi_test::shutdown_imgui(inc::ImGuiPhantasmImpl& impl)
{
    impl.destroy();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}
