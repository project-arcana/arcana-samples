#ifdef PR_BACKEND_VULKAN
#include <nexus/test.hh>

#include "sample.hh"

#include <task-dispatcher/native/thread.hh>

#include <phantasm-renderer/backend/vulkan/BackendVulkan.hh>

namespace
{
pr_test::sample_config get_vk_sample_conf()
{
    pr_test::sample_config sample_conf;
    sample_conf.window_title = "pr::backend::vk liveness";
    sample_conf.shader_ending = "spv";
    sample_conf.align_mip_rows = false;
    return sample_conf;
}
}

TEST("pr::backend::vk sample_pbr", disabled, exclusive)
{
    // td::native::thread_sleep(15000);
    td::launch([&] {
        pr::backend::vk::BackendVulkan backend;
        pr_test::run_pbr_sample(backend, get_vk_sample_conf());
    });
}

TEST("pr::backend::vk sample_cloth", disabled, exclusive)
{
    td::launch([&] {
        pr::backend::vk::BackendVulkan backend;
        pr_test::run_cloth_sample(backend, get_vk_sample_conf());
    });
}

TEST("pr::backend::vk sample_compute", disabled, exclusive)
{
    pr::backend::vk::BackendVulkan backend;
    pr_test::run_compute_sample(backend, get_vk_sample_conf());
}

TEST("pr::backend::vk sample_imgui", disabled, exclusive)
{
    pr::backend::vk::BackendVulkan backend;
    pr_test::run_imgui_sample(backend, get_vk_sample_conf());
}

#endif
