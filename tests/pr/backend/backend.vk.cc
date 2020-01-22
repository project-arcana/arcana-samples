#ifdef PR_BACKEND_VULKAN
#include <nexus/test.hh>

#include "sample.hh"

#include <phantasm-hardware-interface/vulkan/BackendVulkan.hh>

namespace
{
pr_test::sample_config get_vk_sample_conf()
{
    pr_test::sample_config sample_conf;
    sample_conf.window_title = "phi::vk liveness";
    sample_conf.shader_ending = "spv";
    sample_conf.align_mip_rows = false;
    return sample_conf;
}
}

TEST("phi::vk sample_pbr", disabled, exclusive)
{
    td::launch([&] {
        phi::vk::BackendVulkan backend;
        pr_test::run_pbr_sample(backend, get_vk_sample_conf());
    });
}

TEST("phi::vk sample_cloth", disabled, exclusive)
{
    td::launch([&] {
        phi::vk::BackendVulkan backend;
        pr_test::run_cloth_sample(backend, get_vk_sample_conf());
    });
}

TEST("phi::vk sample_raytrace", disabled, exclusive)
{
    phi::vk::BackendVulkan backend;
    pr_test::run_raytracing_sample(backend, get_vk_sample_conf());
}

TEST("phi::vk sample_imgui", disabled, exclusive)
{
    phi::vk::BackendVulkan backend;
    pr_test::run_imgui_sample(backend, get_vk_sample_conf());
}

#endif
