#include <nexus/test.hh>

#include <iostream>

#include <phantasm-renderer/backend/d3d12/Adapter.hh>
#include <phantasm-renderer/backend/d3d12/CommandList.hh>
#include <phantasm-renderer/backend/d3d12/Device.hh>
#include <phantasm-renderer/backend/d3d12/Swapchain.hh>
#include <phantasm-renderer/backend/d3d12/Queue.hh>
#include <phantasm-renderer/backend/d3d12/device_tentative/window.hh>
#include <phantasm-renderer/backend/d3d12/memory/UploadBuffer.hh>
#include <phantasm-renderer/backend/d3d12/memory/DescriptorAllocator.hh>
#include <phantasm-renderer/backend/d3d12/adapter_choice_util.hh>

#include <phantasm-renderer/backend/vulkan/BackendVulkan.hh>
#include <phantasm-renderer/backend/vulkan/layer_extension_util.hh>

TEST("pr backend liveness")
{
#ifdef PR_BACKEND_D3D12
    {
        using namespace pr::backend::d3d12;
        std::cout << std::endl;

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

            pr::backend::d3d12::Device device;
            device.initialize(adapter.getAdapter(), config);

            {
                if (device.hasSM6WaveIntrinsics())
                    std::cout << "Adapter has SM6 wave intrinsics" << std::endl;

                if (device.hasRaytracing())
                    std::cout << "Adapter has Raytracing" << std::endl;
            }

            pr::backend::d3d12::Queue queue;
            queue.initialize(device.getDevice());

            pr::backend::d3d12::UploadBuffer uploadBuffer(device.getDevice());

            for (auto i = 0; i < 5; ++i)
            {
                struct foo
                {
                    float color[4];
                };

                auto alloc = uploadBuffer.alloc<foo>();
                (void)alloc;
            }
            uploadBuffer.reset();

            pr::backend::d3d12::DescriptorAllocator descriptorAllocator(device.getDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            auto desc_alloc = descriptorAllocator.allocate(50);
            desc_alloc.free(1);

            pr::backend::d3d12::Window window;
            window.initialize("Liveness test");

            pr::backend::d3d12::Swapchain swapchain;
            swapchain.initialize(adapter.getFactory(), queue.getQueue(), window.getHandle());

            pr::backend::d3d12::CommandAllocator alloc(device.getDeviceShared());
            {
                auto const com_list = alloc.createCommandList();
            }
            alloc.reset();
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
