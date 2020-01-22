#ifdef PHI_BACKEND_D3D12
#include <nexus/test.hh>

#include <iostream>

#include "sample.hh"

#include <phantasm-hardware-interface/d3d12/BackendD3D12.hh>
#include <phantasm-hardware-interface/d3d12/adapter_choice_util.hh>

namespace
{
pr_test::sample_config get_d3d12_sample_conf()
{
    pr_test::sample_config sample_conf;
    sample_conf.window_title = "phi::d3d12 liveness";
    sample_conf.shader_ending = "dxil";
    sample_conf.align_mip_rows = true;
    return sample_conf;
}
}

TEST("phi::d3d12 sample_pbr", disabled, exclusive)
{
    td::launch([&] {
        phi::d3d12::BackendD3D12 backend;
        pr_test::run_pbr_sample(backend, get_d3d12_sample_conf());
    });
}

TEST("phi::d3d12 sample_raytrace", disabled, exclusive)
{
    phi::d3d12::BackendD3D12 backend;
    pr_test::run_raytracing_sample(backend, get_d3d12_sample_conf());
}

TEST("phi::d3d12 sample_imgui", disabled, exclusive)
{
    phi::d3d12::BackendD3D12 backend;
    pr_test::run_imgui_sample(backend, get_d3d12_sample_conf());
}

TEST("phi::d3d12 adapter choice", disabled, exclusive)
{
    using namespace phi;
    using namespace phi::d3d12;

    // Adapter choice basics
    auto const candidates = get_adapter_candidates();

    auto const vram_choice = get_preferred_gpu(candidates, adapter_preference::highest_vram);
    auto const feature_choice = get_preferred_gpu(candidates, adapter_preference::highest_feature_level);
    auto const integrated_choice = get_preferred_gpu(candidates, adapter_preference::integrated);

    {
        std::cout << "\n\n";
        for (auto const& cand : candidates)
        {
            std::cout << "D3D12 Adapter Candidate #" << cand.index << ": " << cand.description.c_str() << '\n';
            std::cout << " VRAM: " << unsigned(cand.dedicated_video_memory_bytes / (1024 * 1024)) << "MB";
            std::cout << ", RAM: " << unsigned(cand.dedicated_system_memory_bytes / (1024 * 1024)) << "MB";
            std::cout << ", Shared: " << unsigned(cand.shared_system_memory_bytes / (1024 * 1024)) << "MB\n" << std::endl;
        }


        std::cout << "Chose #" << vram_choice << " for highest vram, #" << feature_choice << " for highest feature level and #" << integrated_choice
                  << " for integrated GPU" << std::endl;
    }
}

#endif
