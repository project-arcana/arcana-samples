#include <nexus/test.hh>

#include <array>
#include <iostream>

#include <typed-geometry/tg.hh>

#include <phantasm-renderer/backend/d3d12/BackendD3D12.hh>
#include <phantasm-renderer/backend/d3d12/CommandList.hh>
#include <phantasm-renderer/backend/d3d12/adapter_choice_util.hh>
#include <phantasm-renderer/backend/d3d12/common/d3dx12.hh>
#include <phantasm-renderer/backend/d3d12/common/util.hh>
#include <phantasm-renderer/backend/d3d12/device_tentative/window.hh>
#include <phantasm-renderer/backend/d3d12/memory/DynamicBufferRing.hh>
#include <phantasm-renderer/backend/d3d12/memory/StaticBufferPool.hh>
#include <phantasm-renderer/backend/d3d12/memory/UploadHeap.hh>
#include <phantasm-renderer/backend/d3d12/pipeline_state.hh>
#include <phantasm-renderer/backend/d3d12/resources/resource_creation.hh>
#include <phantasm-renderer/backend/d3d12/resources/simple_vertex.hh>
#include <phantasm-renderer/backend/d3d12/root_signature.hh>
#include <phantasm-renderer/backend/d3d12/shader.hh>

#include <phantasm-renderer/default_config.hh>

#include <phantasm-renderer/backend/vulkan/BackendVulkan.hh>
#include <phantasm-renderer/backend/vulkan/layer_extension_util.hh>


#include <d3d12.h>


#ifdef PR_BACKEND_D3D12
using namespace pr::backend::d3d12;

struct instance_data
{
    wip::srv albedo_tex;

    struct : pr::immediate
    {
        tg::mat4 model;
    } mesh_data;

    struct : wip::constant_buffer
    {
        tg::mat4 view_proj;
    } cam_data;
};

template <class I>
constexpr void introspect(I&& i, instance_data& v)
{
    i(v.mesh_data, "mesh_data");
    i(v.cam_data, "cam_data");
    i(v.albedo_tex, "albedo_tex");
}
#endif


TEST("pr backend liveness")
{
#ifdef PR_BACKEND_D3D12
    {
        // Adapter choice basics
        if constexpr (0)
        {
            auto const candidates = get_adapter_candidates();

            std::cout << std::endl;
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
            Window window;
            window.initialize("Liveness test");

            BackendD3D12 backend;
            {
                d3d12_config config;
                config.enable_validation_extended = true;
                backend.initialize(config, window.getHandle());
            }

            auto const num_backbuffers = backend.mSwapchain.getNumBackbuffers();

            CommandListRing commandListRing;
            commandListRing.initialize(backend, num_backbuffers, 8, backend.mDirectQueue.getQueue().GetDesc());

            ResourceViewAllocator resViewAllocator;
            {
                auto const numCBVs = 2000;
                auto const numSRVs = 2000;
                auto const numUAVs = 10;
                auto const numDSVs = 3;
                auto const numRTVs = 60;
                auto const numSamplers = 20;
                resViewAllocator.initialize(backend.mDevice.getDevice(), numCBVs, numSRVs, numUAVs, numDSVs, numRTVs, numSamplers);
            }

            DynamicBufferRing dynamicBufferRing;
            {
                auto const size = 20 * 1024 * 1024;
                dynamicBufferRing.initialize(backend.mDevice.getDevice(), num_backbuffers, size);
            }

            UploadHeap uploadHeap;
            {
                auto const size = 1000 * 1024 * 1024;
                uploadHeap.initialize(&backend, size);
            }


            {
                // Shader compilation
                //
                cc::capped_vector<shader, 6> shaders;
                {
                    auto& vert = shaders.emplace_back();
                    vert = compile_shader_from_file("testdata/shader.hlsl", shader_domain::vertex);

                    auto& pixel = shaders.emplace_back();
                    pixel = compile_shader_from_file("testdata/shader.hlsl", shader_domain::pixel);

                    CC_RUNTIME_ASSERT(vert.is_valid() && pixel.is_valid() && "failed to load shaders");
                }

                // Resource setup
                //
                resource material;
                resource_view material_srv = resViewAllocator.allocCBV_SRV_UAV(1);
                {
                    material = create_texture2d_from_file(backend.mAllocator, backend.mDevice.getDevice(), uploadHeap, "testdata/uv_checker.png");
                    make_srv(material, material_srv, 0);
                }

                resource mesh_vertices;
                resource mesh_indices;
                D3D12_VERTEX_BUFFER_VIEW mesh_vbv;
                D3D12_INDEX_BUFFER_VIEW mesh_ibv;
                unsigned mesh_num_indices = 0;

                {
                    auto const mesh_data = load_obj_mesh("testdata/apollo.obj");
                    mesh_num_indices = unsigned(mesh_data.indices.size());

                    mesh_indices = create_buffer_from_data<int>(backend.mAllocator, uploadHeap, mesh_data.indices);
                    mesh_vertices = create_buffer_from_data<simple_vertex>(backend.mAllocator, uploadHeap, mesh_data.vertices);

                    mesh_ibv = make_index_buffer_view(mesh_indices, sizeof(int));
                    mesh_vbv = make_vertex_buffer_view(mesh_vertices, sizeof(simple_vertex));
                }

                uploadHeap.flushAndFinish();

                shared_com_ptr<ID3D12RootSignature> root_sig = get_root_signature<instance_data>(backend.mDevice.getDevice());

                auto const input_layout = get_vertex_attributes<pr::backend::d3d12::simple_vertex>();
                shared_com_ptr<ID3D12PipelineState> pso
                    = create_pipeline_state(backend.mDevice.getDevice(), input_layout, shaders, root_sig, pr::default_config);

                resource depth_buffer;
                resource_view depth_buffer_dsv = resViewAllocator.allocDSV(1);

                resource color_buffer;
                resource_view color_buffer_rtv = resViewAllocator.allocRTV(1);

                auto const on_resize_func = [&](int w, int h) {
                    std::cout << "resize to " << w << "x" << h << std::endl;

                    depth_buffer = create_depth_stencil(backend.mAllocator, w, h, DXGI_FORMAT_D32_FLOAT);
                    make_dsv(depth_buffer, depth_buffer_dsv, 0);

                    color_buffer = create_render_target(backend.mAllocator, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
                    make_rtv(color_buffer, color_buffer_rtv, 0);

                    backend.mSwapchain.onResize(w, h);
                };

                // Main loop
                while (!window.isRequestingClose())
                {
                    window.pollEvents();
                    if (window.isPendingResize())
                    {
                        if (!window.isMinimized())
                            on_resize_func(window.getWidth(), window.getHeight());
                        window.clearPendingResize();
                    }

                    if (!window.isMinimized())
                    {
                        dynamicBufferRing.onBeginFrame();


                        // ... do something else ...

                        backend.mSwapchain.waitForSwapchain();
                        commandListRing.onBeginFrame();

                        // ... render to swapchain ...
                        {
                            auto* const current_backbuffer_resource = backend.mSwapchain.getCurrentBackbufferResource();
                            auto* const command_list = commandListRing.acquireCommandList();

                            cc::array const desc_heaps = {resViewAllocator.getCBV_SRV_UAVHeap(), resViewAllocator.getSamplerHeap()};
                            command_list->SetDescriptorHeaps(unsigned(desc_heaps.size()), desc_heaps.data());

                            command_list->SetPipelineState(pso);
                            command_list->SetGraphicsRootSignature(root_sig);
                            util::set_viewport(command_list, backend.mSwapchain.getBackbufferSize());

                            {
                                auto const barrier_desc = CD3DX12_RESOURCE_BARRIER::Transition(
                                    current_backbuffer_resource, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
                                command_list->ResourceBarrier(1, &barrier_desc);
                            }

                            auto& backbuffer_rtv = backend.mSwapchain.getCurrentBackbufferRTV();
                            auto const depth_buffer_dsv_cpu = depth_buffer_dsv.get_cpu();
                            auto const color_buffer_rtv_cpu = color_buffer_rtv.get_cpu();
                            command_list->OMSetRenderTargets(1, &backbuffer_rtv, 1, &depth_buffer_dsv_cpu);

                            constexpr float clearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
                            command_list->ClearRenderTargetView(backbuffer_rtv, clearColor, 0, nullptr);
                            command_list->ClearDepthStencilView(depth_buffer_dsv_cpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

                            // Mesh binding
                            command_list->IASetIndexBuffer(&mesh_ibv);
                            command_list->IASetVertexBuffers(0, 1, &mesh_vbv);
                            command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


                            // Root CBV
                            {
                                struct framedata
                                {
                                    tg::mat4 view_proj;
                                };

                                framedata* cb_cpu;
                                D3D12_GPU_VIRTUAL_ADDRESS cb_gpu;
                                dynamicBufferRing.allocConstantBuffer(cb_cpu, cb_gpu);

                                {
                                    auto const proj = tg::perspective_directx(60_deg, window.getWidth() / float(window.getHeight()), 0.1f, 100.f);
                                    auto target = tg::pos3(0, 1, 0);
                                    auto camPos = tg::pos3(1, 1, 1) * 5.f;
                                    auto view = tg::look_at_directx(camPos, target, tg::vec3(0, 1, 0));
                                    cb_cpu->view_proj = proj * view;
                                }

                                command_list->SetGraphicsRootConstantBufferView(1, cb_gpu);
                            }

                            // Root descriptor table
                            command_list->SetGraphicsRootDescriptorTable(2, material_srv.get_gpu());

                            for (auto modelpos : {tg::vec3(0, 0, 0), tg::vec3(3, 0, 0), tg::vec3(0, 3, 0), tg::vec3(0, 0, 3)})
                            {
                                // Root constants
                                {
                                    static float increasing_val = 0.f;
                                    increasing_val += 0.0001f;
                                    auto model = tg::translation(modelpos) * tg::rotation_y(tg::radians(increasing_val));

                                    command_list->SetGraphicsRoot32BitConstants(0, 16, tg::data_ptr(model), 0);
                                }

                                command_list->DrawIndexedInstanced(mesh_num_indices, 1, 0, 0, 0);
                            }

                            {
                                // transition backbuffer back to present state from render target state
                                auto const barrier_desc = CD3DX12_RESOURCE_BARRIER::Transition(
                                    current_backbuffer_resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                                command_list->ResourceBarrier(1, &barrier_desc);
                            }

                            command_list->Close();

                            ID3D12CommandList* submits[] = {command_list};
                            backend.mDirectQueue.getQueue().ExecuteCommandLists(1, submits);
                        }

                        backend.mSwapchain.present();
                    }
                }

                // Cleanup before dtors get called
                backend.flushGPU();
                backend.mSwapchain.setFullscreen(false);
            }
        }
    }
#endif

#ifdef PR_BACKEND_VULKAN
    if constexpr (0)
    {
        pr::backend::vk::vulkan_config config;
        pr::backend::vk::BackendVulkan bv;
        bv.initialize(config);
    }
#endif
}
