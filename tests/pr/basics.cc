#include <nexus/test.hh>

#ifdef PR_BACKEND_D3D12
#include <iostream>

#include <phantasm-renderer/backend/d3d12/Adapter.hh>
#include <phantasm-renderer/backend/d3d12/adapter_choice_util.hh>
#include <phantasm-renderer/backend/vulkan/BackendVulkan.hh>
#include <phantasm-renderer/backend/vulkan/layer_extension_util.hh>


TEST("pr backend liveness")
{
#ifdef PR_BACKEND_D3D12
    {
        using namespace pr::backend::d3d12;

        // Adapter choice basics
        {
            auto const candidates = get_adapter_candidates();

            for (auto const& cand : candidates)
            {
                std::cout << "D3D12 Adapter Candidate #" << cand.index << ": " << cand.description << '\n';
                std::cout << " VRAM: " << unsigned(cand.dedicated_video_memory_bytes / (1024 * 1024)) << "MB";
                std::cout << ", RAM: " << unsigned(cand.dedicated_system_memory_bytes / (1024 * 1024)) << "MB";
                std::cout << ", Shared: " << unsigned(cand.shared_system_memory_bytes / (1024 * 1024)) << "MB\n" << std::endl;
            }

            auto const vram_choice = get_preferred_adapter_index(candidates, adapter_preference::highest_vram);
            auto const feature_choice = get_preferred_adapter_index(candidates, adapter_preference::highest_feature_level);
            auto const integrated_choice = get_preferred_adapter_index(candidates, adapter_preference::integrated);

            std::cout << "Chose #" << vram_choice << " for highest vram, #" << feature_choice << " for highest feature level and #"
                      << integrated_choice << " for integrated GPU" << std::endl;
        }

        {
            pr::backend::d3d12::d3d12_config config;
            config.enable_validation = true;
            config.enable_gpu_validation = true;

            pr::backend::d3d12::Adapter adapter;
            adapter.initialize(config);

            std::cout << "Created D3D12 adapter" << std::endl;

            if (adapter.getCapabilities().has_sm6_wave_intrinsics)
                std::cout << "Adapter has SM6 wave intrinsics" << std::endl;

            if (adapter.getCapabilities().has_raytracing)
                std::cout << "Adapter has Raytracing" << std::endl;
        }
    }
#endif

#ifdef PR_BACKEND_VULKAN
    {
        pr::backend::vk::vulkan_config config;
        pr::backend::vk::BackendVulkan bv;
        bv.initialize(config);
    }
#endif
}
#endif
