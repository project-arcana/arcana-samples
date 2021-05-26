#ifdef PHI_BACKEND_VULKAN
#include <nexus/app.hh>
#include <nexus/test.hh>

#include "sample.hh"

#include <phantasm-hardware-interface/vulkan/BackendVulkan.hh>

namespace
{
phi_test::sample_config get_vk_sample_conf()
{
    phi_test::sample_config sample_conf;
    sample_conf.window_title = "PHI Vulkan Sample";
    sample_conf.shader_ending = "spv";
    sample_conf.align_mip_rows = false;
    return sample_conf;
}
}

APP("vk_pbr")
{
    td::launch([&] {
        phi::vk::BackendVulkan backend;
        phi_test::run_pbr_sample(backend, get_vk_sample_conf());
    });
}

APP("vk_async_compute")
{
    td::launch([&] {
        phi::vk::BackendVulkan backend;
        phi_test::run_nbody_async_compute_sample(backend, get_vk_sample_conf());
    });
}

APP("vk_rt")
{
    phi::vk::BackendVulkan backend;
    phi_test::run_raytracing_sample(backend, get_vk_sample_conf());
}

APP("vk_imgui")
{
    phi::vk::BackendVulkan backend;
    phi_test::run_imgui_sample(backend, get_vk_sample_conf());
}

APP("vk_bindless")
{
    phi::vk::BackendVulkan backend;
    phi_test::run_bindless_sample(backend, get_vk_sample_conf());
}

#endif
