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

void phi_test::preprocess_spherical_harmonics(const phi_test::SHVecColor& sh_irradiance, tg::span<tg::vec4> out_data)
{
    CC_ASSERT(out_data.size() == 7 && "out data has wrong size");

    // ref http://www.ppsloan.org/publications/StupidSH36.pdf

    float const pi_sqrt = tg::sqrt(tg::pi_scalar<float>);
    float const c0 = 1.0f / (2 * pi_sqrt);
    float const c1 = tg::sqrt(3.f) / (3 * pi_sqrt);
    float const c2 = tg::sqrt(15.f) / (8 * pi_sqrt);
    float const c3 = tg::sqrt(5.f) / (16 * pi_sqrt);
    float const c4 = .5f * c2;

    out_data[0].x = -c1 * sh_irradiance.R.val[3];
    out_data[0].y = -c1 * sh_irradiance.R.val[1];
    out_data[0].z = c1 * sh_irradiance.R.val[2];
    out_data[0].w = c0 * sh_irradiance.R.val[0] - c3 * sh_irradiance.R.val[6];

    out_data[1].x = -c1 * sh_irradiance.G.val[3];
    out_data[1].y = -c1 * sh_irradiance.G.val[1];
    out_data[1].z = c1 * sh_irradiance.G.val[2];
    out_data[1].w = c0 * sh_irradiance.G.val[0] - c3 * sh_irradiance.G.val[6];

    out_data[2].x = -c1 * sh_irradiance.B.val[3];
    out_data[2].y = -c1 * sh_irradiance.B.val[1];
    out_data[2].z = c1 * sh_irradiance.B.val[2];
    out_data[2].w = c0 * sh_irradiance.B.val[0] - c3 * sh_irradiance.B.val[6];

    out_data[3].x = c2 * sh_irradiance.R.val[4];
    out_data[3].y = -c2 * sh_irradiance.R.val[5];
    out_data[3].z = 3 * c3 * sh_irradiance.R.val[6];
    out_data[3].w = -c2 * sh_irradiance.R.val[7];

    out_data[4].x = c2 * sh_irradiance.G.val[4];
    out_data[4].y = -c2 * sh_irradiance.G.val[5];
    out_data[4].z = 3 * c3 * sh_irradiance.G.val[6];
    out_data[4].w = -c2 * sh_irradiance.G.val[7];

    out_data[5].x = c2 * sh_irradiance.B.val[4];
    out_data[5].y = -c2 * sh_irradiance.B.val[5];
    out_data[5].z = 3 * c3 * sh_irradiance.B.val[6];
    out_data[5].w = -c2 * sh_irradiance.B.val[7];

    out_data[6].x = c4 * sh_irradiance.R.val[8];
    out_data[6].y = c4 * sh_irradiance.G.val[8];
    out_data[6].z = c4 * sh_irradiance.B.val[8];
    out_data[6].w = 1;
}
