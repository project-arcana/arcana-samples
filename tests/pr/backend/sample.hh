#pragma once

#include <task-dispatcher/td.hh>

#include <phantasm-renderer/backend/Backend.hh>

namespace pr_test
{
inline constexpr auto num_render_threads = 8;

inline auto const get_backend_config = [] {
    pr::backend::backend_config config;
    config.present_mode = pr::backend::present_mode::allow_tearing;
    config.adapter_preference = pr::backend::adapter_preference::highest_vram;
    config.num_threads = td::system::num_logical_cores();

    config.validation =
#ifdef NDEBUG
        pr::backend::validation_level::off;
#else
        pr::backend::validation_level::on_extended_dred;
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

void run_sample(pr::backend::Backend& backend, pr::backend::backend_config const& config, sample_config const& sample_config);
}
