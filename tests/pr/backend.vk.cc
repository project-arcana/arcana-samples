#ifdef PR_BACKEND_VULKAN
#include <nexus/test.hh>

#include "backend_common.hh"

#include <phantasm-renderer/backend/vulkan/BackendVulkan.hh>

TEST("pr::backend::vk liveness", disabled, exclusive)
{
    td::launch([&] {
        pr::backend::vk::BackendVulkan backend;

        pr_test::sample_config sample_conf;
        sample_conf.window_title = "pr::backend::vk liveness";
        sample_conf.path_render_vs = "res/pr/liveness_sample/shader/spirv/vertex.spv";
        sample_conf.path_render_ps = "res/pr/liveness_sample/shader/spirv/pixel.spv";
        sample_conf.path_blit_vs = "res/pr/liveness_sample/shader/spirv/vertex_blit.spv";
        sample_conf.path_blit_ps = "res/pr/liveness_sample/shader/spirv/pixel_blit.spv";
        sample_conf.align_mip_rows = false;

        pr_test::run_sample(backend, pr_test::get_backend_config(), sample_conf);
    });
    return;
}

#endif
