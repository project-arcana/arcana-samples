#include <nexus/test.hh>

#include <array>
#include <iostream>

#include <typed-geometry/tg.hh>

#include <phantasm-renderer/backend/d3d12/BackendD3D12.hh>
#include <phantasm-renderer/backend/d3d12/CommandList.hh>
#include <phantasm-renderer/backend/d3d12/Device.hh>
#include <phantasm-renderer/backend/d3d12/Queue.hh>
#include <phantasm-renderer/backend/d3d12/Swapchain.hh>
#include <phantasm-renderer/backend/d3d12/adapter_choice_util.hh>
#include <phantasm-renderer/backend/d3d12/device_tentative/window.hh>
#include <phantasm-renderer/backend/d3d12/resources/Texture.hh>
#include <phantasm-renderer/backend/d3d12/resources/simple_vertex.hh>
#include <phantasm-renderer/backend/d3d12/shader/ShaderCompiler.hh>

#include <phantasm-renderer/backend/d3d12/DynamicBufferRing.hh>
#include <phantasm-renderer/backend/d3d12/ResourceViewHeaps.hh>
#include <phantasm-renderer/backend/d3d12/StaticBufferPool.hh>
#include <phantasm-renderer/backend/d3d12/UploadHeap.hh>

#include <phantasm-renderer/backend/d3d12/common/d3dx12.hh>

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
            Window window;
            window.initialize("Liveness test");

            BackendD3D12 backend;
            {
                d3d12_config config;
                config.enable_validation_extended = true;
                backend.initialize(config, window.getHandle());

                {
                    if (backend.mDevice.hasSM6WaveIntrinsics())
                        std::cout << "Adapter has SM6 wave intrinsics" << std::endl;

                    if (backend.mDevice.hasRaytracing())
                        std::cout << "Adapter has Raytracing" << std::endl;
                }
            }

            auto const num_backbuffers = backend.mSwapchain.getNumBackbuffers();

            CommandListRing commandListRing;
            commandListRing.initialize(backend, num_backbuffers, 8, backend.mDirectQueue.getQueue().GetDesc());

            ResourceViewHeaps resViewHeaps;
            {
                auto const numCBVs = 2000;
                auto const numSRVs = 2000;
                auto const numUAVs = 10;
                auto const numDSVs = 3;
                auto const numRTVs = 60;
                auto const numSamplers = 20;
                resViewHeaps.initialize(backend.mDevice.getDevice(), numCBVs, numSRVs, numUAVs, numDSVs, numRTVs, numSamplers);
            }

            DynamicBufferRing dynamicBufferRing;
            {
                auto const size = 20 * 1024 * 1024;
                dynamicBufferRing.initialize(backend.mDevice.getDevice(), num_backbuffers, size);
            }

            StaticBufferPool staticBufferPool;
            {
                auto const size = 128 * 1024 * 1024;
                staticBufferPool.initialize(backend.mDevice.getDevice(), size, true, "Static generic");
            }

            UploadHeap uploadHeap;
            {
                auto const size = 1000 * 1024 * 1024;
                uploadHeap.initialize(&backend, size);
            }


            {
                // Resource setup
                D3D12_SHADER_BYTECODE vertex_shader;
                D3D12_SHADER_BYTECODE pixel_shader;
                {
                    auto const s1 = compile_shader_from_file("testdata/shader.hlsl", "mainVS", "vs_5_0", vertex_shader);
                    auto const s2 = compile_shader_from_file("testdata/shader.hlsl", "mainPS", "ps_5_0", pixel_shader);

                    if (!s1 || !s2)
                    {
                        CC_RUNTIME_ASSERT(false && "failed to load shaders");
                    }
                }

                shared_com_ptr<ID3D12RootSignature> root_sig;

                {
                    D3D12_ROOT_PARAMETER mvp_param = {};
                    mvp_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                    mvp_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
                    mvp_param.Constants.Num32BitValues = 16;
                    mvp_param.Constants.RegisterSpace = 0;
                    mvp_param.Constants.ShaderRegister = 0;

                    D3D12_ROOT_PARAMETER params[] = {mvp_param};

                    D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
                    root_sig_desc.NumParameters = 1;
                    root_sig_desc.pParameters = params;
                    root_sig_desc.NumStaticSamplers = 0;
                    root_sig_desc.pStaticSamplers = nullptr;
                    root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

                    ID3DBlob* serialized_root_sig = nullptr;
                    PR_D3D12_VERIFY(D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &serialized_root_sig, nullptr));

                    PR_D3D12_VERIFY(backend.mDevice.getDevice().CreateRootSignature(0, serialized_root_sig->GetBufferPointer(),
                                                                                    serialized_root_sig->GetBufferSize(), PR_COM_WRITE(root_sig)));
                    serialized_root_sig->Release();
                }

                shared_com_ptr<ID3D12PipelineState> pso;

                {
                    auto const input_layout = get_vertex_attributes<pr::backend::d3d12::simple_vertex>();

                    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {}; // explicit init on purpose
                    pso_desc.InputLayout = {input_layout.data(), UINT(input_layout.size())};
                    pso_desc.pRootSignature = root_sig;
                    pso_desc.VS = vertex_shader;
                    pso_desc.PS = pixel_shader;

                    pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
                    pso_desc.BlendState.RenderTarget[0] = D3D12_RENDER_TARGET_BLEND_DESC{
                        false, FALSE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL};

                    pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
                    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

                    pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
                    pso_desc.DepthStencilState.DepthEnable = true;
                    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

                    pso_desc.SampleMask = UINT_MAX;
                    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                    pso_desc.NumRenderTargets = 1;
                    pso_desc.RTVFormats[0] = backend.mSwapchain.getBackbufferFormat();
                    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT; // TODO store this
                    pso_desc.SampleDesc.Count = 1;
                    pso_desc.NodeMask = 0;
                    pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

                    PR_D3D12_VERIFY(backend.mDevice.getDevice().CreateGraphicsPipelineState(&pso_desc, PR_COM_WRITE(pso)));
                }

                D3D12_VERTEX_BUFFER_VIEW mesh_vbv;
                unsigned mesh_num_verts = 0;
                {
                    cc::array raw_vertex_data = {
                        simple_vertex{{0.0f, 0.7f, 0.0f}, {1.0f, 0.0f, 0.0f}},   //
                        simple_vertex{{-0.4f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}}, //
                        simple_vertex{{0.4f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},  //
                        simple_vertex{{0.5f, 0.9f, 0.0f}, {1.0f, 0.0f, 0.0f}},   //
                        simple_vertex{{-0.2f, -0.2f, 0.0f}, {0.0f, 1.0f, 0.0f}}, //
                        simple_vertex{{0.4f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},  //
                    };

                    mesh_num_verts = unsigned(raw_vertex_data.size());
                    staticBufferPool.allocVertexBuffer(mesh_num_verts, sizeof(simple_vertex), raw_vertex_data.data(), &mesh_vbv);

                    {
                        auto upload_cmd_list = commandListRing.acquireCommandList();
                        staticBufferPool.uploadData(upload_cmd_list);
                        upload_cmd_list->Close();

                        ID3D12CommandList* submits[] = {upload_cmd_list};
                        backend.mDirectQueue.getQueue().ExecuteCommandLists(1, submits);
                        backend.flushGPU();
                    }
                }

                D3D12_VIEWPORT viewport = {0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
                D3D12_RECT scissor_rect = {};

                Texture depth_buffer;
                RViewDSV depth_buffer_dsv;
                resViewHeaps.allocDSV(1, &depth_buffer_dsv);

                auto const on_resize_func = [&](int w, int h) {
                    std::cout << "resize to " << w << "x" << h << std::endl;

                    auto depth_buffer_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, UINT(w), UINT(h), 1, 0, 1, 0,
                                                                          D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
                    depth_buffer.initDepthStencil(backend.mDevice.getDevice(), "depth buffer", &depth_buffer_desc);
                    depth_buffer.createDSV(0, depth_buffer_dsv);

                    backend.mSwapchain.onResize(w, h);

                    viewport.Width = float(w);
                    viewport.Height = float(h);

                    scissor_rect.right = LONG(w);
                    scissor_rect.bottom = LONG(h);
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
                        // ... do something else ...

                        backend.mSwapchain.waitForSwapchain();
                        commandListRing.onBeginFrame();

                        // ... render to swapchain ...
                        {
                            auto* const current_backbuffer_resource = backend.mSwapchain.getCurrentBackbufferResource();
                            auto* const command_list = commandListRing.acquireCommandList();

                            command_list->SetPipelineState(pso);
                            command_list->SetGraphicsRootSignature(root_sig);
                            command_list->RSSetViewports(1, &viewport);
                            command_list->RSSetScissorRects(1, &scissor_rect);

                            {
                                auto const barrier_desc = CD3DX12_RESOURCE_BARRIER::Transition(
                                    current_backbuffer_resource, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
                                command_list->ResourceBarrier(1, &barrier_desc);
                            }

                            auto& backbuffer_rtv = backend.mSwapchain.getCurrentBackbufferRTV();
                            auto const depth_buffer_dsv_cpu = depth_buffer_dsv.getCPU();
                            command_list->OMSetRenderTargets(1, &backbuffer_rtv, 1, &depth_buffer_dsv_cpu);

                            float constexpr clearColor[] = {0.1f, 0.1125f, 0.115f, 1.0f};
                            command_list->ClearRenderTargetView(backbuffer_rtv, clearColor, 0, nullptr);
                            command_list->ClearDepthStencilView(depth_buffer_dsv_cpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                            command_list->IASetVertexBuffers(0, 1, &mesh_vbv);
                            command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                            {
                                //                                auto const viewport = swapchain.getBackbufferSize();
                                //                                auto const proj = tg::perspective(40_deg, window.getWidth() / float(window.getHeight()), 0.1f, 100.f);

                                //                                auto target = tg::pos3::zero;
                                //                                auto camPos = tg::pos3(0.5f, .75f, 2.f) * 1.5f;
                                //                                auto view = tg::look_at(camPos, camPos - target, tg::vec3(0, 1, 0));
                                //                                auto model = tg::rotation_y(tg::radians(increasingValue));
                                //                                auto const mvp = proj * view * model;

                                // ;
                                auto const identity = tg::mat4::identity;
                                command_list->SetGraphicsRoot32BitConstants(0, 16, tg::data_ptr(identity), 0);
                            }

                            command_list->DrawInstanced(mesh_num_verts, 1, 0, 0);

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
    {
        pr::backend::vk::vulkan_config config;
        pr::backend::vk::BackendVulkan bv;
        bv.initialize(config);
    }
#endif
}
