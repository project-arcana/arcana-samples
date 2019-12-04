#ifdef PR_BACKEND_D3D12
#include <nexus/test.hh>

#include "backend_common.hh"

#include <phantasm-renderer/backend/d3d12/BackendD3D12.hh>
#include <phantasm-renderer/backend/d3d12/adapter_choice_util.hh>

TEST("pr::backend::d3d12 liveness", exclusive)
{
    td::launch([&] {
        pr::backend::d3d12::BackendD3D12 backend;

        pr_test::sample_config sample_conf;
        sample_conf.window_title = "pr::backend::d3d12 liveness";
        sample_conf.path_render_vs = "res/pr/liveness_sample/shader/dxil/vertex.dxil";
        sample_conf.path_render_ps = "res/pr/liveness_sample/shader/dxil/pixel.dxil";
        sample_conf.path_blit_vs = "res/pr/liveness_sample/shader/dxil/blit_vertex.dxil";
        sample_conf.path_blit_ps = "res/pr/liveness_sample/shader/dxil/blit_pixel.dxil";

        pr_test::run_sample(backend, pr_test::get_backend_config(), sample_conf);
    });
}

TEST("pr::backend::d3d12 adapter choice", exclusive)
{
    auto constexpr mute = false;
    if (0)
    {
        using namespace pr::backend;
        using namespace pr::backend::d3d12;

        // Adapter choice basics
        auto const candidates = get_adapter_candidates();

        auto const vram_choice = get_preferred_adapter_index(candidates, adapter_preference::highest_vram);
        auto const feature_choice = get_preferred_adapter_index(candidates, adapter_preference::highest_feature_level);
        auto const integrated_choice = get_preferred_adapter_index(candidates, adapter_preference::integrated);

        if constexpr (!mute)
        {
            std::cout << "\n\n";
            for (auto const& cand : candidates)
            {
                std::cout << "D3D12 Adapter Candidate #" << cand.index << ": " << cand.description << '\n';
                std::cout << " VRAM: " << unsigned(cand.dedicated_video_memory_bytes / (1024 * 1024)) << "MB";
                std::cout << ", RAM: " << unsigned(cand.dedicated_system_memory_bytes / (1024 * 1024)) << "MB";
                std::cout << ", Shared: " << unsigned(cand.shared_system_memory_bytes / (1024 * 1024)) << "MB\n" << std::endl;
            }


            std::cout << "Chose #" << vram_choice << " for highest vram, #" << feature_choice << " for highest feature level and #"
                      << integrated_choice << " for integrated GPU" << std::endl;
        }
    }
}

#endif