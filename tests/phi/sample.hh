#pragma once

#include <task-dispatcher/td.hh>

#include <phantasm-hardware-interface/config.hh>
#include <phantasm-hardware-interface/fwd.hh>

namespace phi_test
{
inline constexpr auto num_render_threads = 8;

phi::backend_config get_backend_config();

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

void run_pathtracing_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& backend_config = get_backend_config());

void run_nbody_async_compute_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& config = get_backend_config());

void run_bindless_sample(phi::Backend& backend, sample_config const& sample_config, phi::backend_config const& config = get_backend_config());
}
