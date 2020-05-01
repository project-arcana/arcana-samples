#pragma once

#include <task-dispatcher/td.hh>

#include <phantasm-hardware-interface/Backend.hh>
#include <phantasm-hardware-interface/config.hh>

namespace phi_test
{
inline constexpr auto num_render_threads = 8;

inline auto const get_backend_config = [] {
    phi::backend_config config;
    config.present = phi::present_mode::allow_tearing;
    config.adapter = phi::adapter_preference::highest_vram;
    config.num_threads = td::is_scheduler_alive() ? td::get_current_num_threads() : 1;

    config.validation =
#ifdef NDEBUG
        phi::validation_level::off;
#else
        phi::validation_level::on_extended;
#endif

    return config;
};

// Sample code
struct sample_config
{
    char const* window_title;
    char const* shader_ending;

    /// whether to align MIP rows by 256 bytes (D3D12: yes, Vulkan: no)
    bool align_mip_rows;
};

void run_pbr_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& backend_config = get_backend_config());

void run_imgui_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& backend_config = get_backend_config());

void run_raytracing_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& backend_config = get_backend_config());

void run_nbody_async_compute_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& config = get_backend_config());
}
