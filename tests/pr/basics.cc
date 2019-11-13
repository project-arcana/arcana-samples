#include <array>
#include <iostream>
#include <nexus/test.hh>
#include <phantasm-renderer/backend/d3d12/BackendD3D12.hh>
#include <phantasm-renderer/backend/d3d12/CommandList.hh>
#include <phantasm-renderer/backend/d3d12/adapter_choice_util.hh>
#include <phantasm-renderer/backend/d3d12/common/d3dx12.hh>
#include <phantasm-renderer/backend/d3d12/common/util.hh>
#include <phantasm-renderer/backend/d3d12/device_tentative/timer.hh>
#include <phantasm-renderer/backend/d3d12/device_tentative/window.hh>
#include <phantasm-renderer/backend/d3d12/memory/DynamicBufferRing.hh>
#include <phantasm-renderer/backend/d3d12/memory/StaticBufferPool.hh>
#include <phantasm-renderer/backend/d3d12/memory/UploadHeap.hh>
#include <phantasm-renderer/backend/d3d12/pipeline_state.hh>
#include <phantasm-renderer/backend/d3d12/resources/resource_creation.hh>
#include <phantasm-renderer/backend/d3d12/resources/simple_vertex.hh>
#include <phantasm-renderer/backend/d3d12/root_signature.hh>
#include <phantasm-renderer/backend/d3d12/shader.hh>
#include <phantasm-renderer/backend/vulkan/BackendVulkan.hh>
#include <phantasm-renderer/backend/vulkan/layer_extension_util.hh>
#include <phantasm-renderer/default_config.hh>
#include <typed-geometry/tg.hh>


#ifdef PR_BACKEND_D3D12
using namespace pr::backend::d3d12;

struct pass_data
{
    wip::srv albedo_tex;
    tg::mat4 view_proj;
};

struct instance_data : pr::immediate
{
    tg::mat4 model;
};

template <class I>
constexpr void introspect(I&& i, pass_data& v)
{
    i(v.albedo_tex, "albedo_tex");
    i(v.view_proj, "view_proj");
}

template <class I>
constexpr void introspect(I&& i, instance_data& v)
{
    i(v.model, "model");
}
#endif


TEST("pr backend liveness", exclusive)
{
#ifdef PR_BACKEND_D3D12
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
        DescriptorAllocator descManager;
        DynamicBufferRing dynamicBufferRing;
        UploadHeap uploadHeap;
        {
            auto const num_command_lists = 8;
            auto const num_shader_resources = 2000 + 2000 + 10;
            auto const num_dsvs = 3;
            auto const num_rtvs = 60;
            auto const num_samplers = 20;
            auto const dynamic_buffer_size = 20 * 1024 * 1024;
            auto const upload_size = 1000 * 1024 * 1024;

            commandListRing.initialize(backend, num_backbuffers, num_command_lists, D3D12_COMMAND_LIST_TYPE_DIRECT);
            descManager.initialize(backend.mDevice.getDevice(), num_shader_resources, num_dsvs, num_rtvs, num_samplers, num_backbuffers);
            dynamicBufferRing.initialize(backend.mDevice.getDevice(), num_backbuffers, dynamic_buffer_size);
            uploadHeap.initialize(&backend, upload_size);
        }


        {
            // Shader compilation
            //
            cc::capped_vector<shader, 6> shaders;
            {
                shaders.push_back(compile_shader_from_file("testdata/shader.hlsl", shader_domain::vertex));
                shaders.push_back(compile_shader_from_file("testdata/shader.hlsl", shader_domain::pixel));

                for (auto const& s : shaders)
                    CC_RUNTIME_ASSERT(s.is_valid() && "failed to load shaders");
            }

            // Resource setup
            //
            resource material;
            cpu_cbv_srv_uav mat_srv = descManager.allocCBV_SRV_UAV();
            {
                material = create_texture2d_from_file(backend.mAllocator, backend.mDevice.getDevice(), uploadHeap, "testdata/uv_checker.png");
                make_srv(material, mat_srv.handle);

                util::transition_barrier(uploadHeap.getCommandList(), material.raw, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

                util::transition_barrier(uploadHeap.getCommandList(), mesh_indices.raw, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
                util::transition_barrier(uploadHeap.getCommandList(), mesh_vertices.raw, D3D12_RESOURCE_STATE_COPY_DEST,
                                         D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            }

            uploadHeap.flushAndFinish();

            root_signature root_sig;
            cc::capped_vector<root_sig_payload_size, 2> payload_sizes;
            {
                payload_sizes.push_back(get_payload_size<pass_data>());
                payload_sizes.push_back(get_payload_size<instance_data>());

                root_sig.initialize(backend.mDevice.getDevice(), payload_sizes);
            }

            resource depth_buffer;
            cpu_dsv depth_buffer_dsv = descManager.allocDSV();

            resource color_buffer;
            cpu_rtv color_buffer_rtv = descManager.allocRTV();

            shared_com_ptr<ID3D12PipelineState> pso;
            {
                wip::framebuffer_format framebuf;
                framebuf.depth_target.push_back(DXGI_FORMAT_D32_FLOAT);
                framebuf.render_targets.push_back(backend.mSwapchain.getBackbufferFormat());

                auto const input_layout = get_vertex_attributes<pr::backend::d3d12::simple_vertex>();
                pso = create_pipeline_state(backend.mDevice.getDevice(), input_layout, shaders, root_sig.raw_root_sig, pr::default_config, framebuf);
            }

            auto const on_resize_func = [&](int w, int h) {
                std::cout << "resize to " << w << "x" << h << std::endl;

                depth_buffer = create_depth_stencil(backend.mAllocator, w, h, DXGI_FORMAT_D32_FLOAT);
                make_dsv(depth_buffer, depth_buffer_dsv.handle);

                color_buffer = create_render_target(backend.mAllocator, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
                make_rtv(color_buffer, color_buffer_rtv.handle);

                backend.mSwapchain.onResize(w, h);
            };

            // Main loop
            Timer timer;
            double run_time = 0.0;

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
                    auto const frametime = timer.getElapsedTime();
                    run_time += frametime;
                    timer.reset();
                    dynamicBufferRing.onBeginFrame();
                    descManager.onBeginFrame();
                    commandListRing.onBeginFrame();


                    // ... do something else ...

                    backend.mSwapchain.waitForSwapchain();

                    // ... render to swapchain ...
                    {
                        auto* const current_backbuffer_resource = backend.mSwapchain.getCurrentBackbufferResource();

                        auto* const command_list = commandListRing.acquireCommandList();

                        descManager.setHeaps(*command_list);
                        command_list->SetPipelineState(pso);
                        command_list->SetGraphicsRootSignature(root_sig.raw_root_sig);
                        util::set_viewport(command_list, backend.mSwapchain.getBackbufferSize());

                        {
                            auto const barrier_desc = CD3DX12_RESOURCE_BARRIER::Transition(current_backbuffer_resource, D3D12_RESOURCE_STATE_PRESENT,
                                                                                           D3D12_RESOURCE_STATE_RENDER_TARGET);
                            command_list->ResourceBarrier(1, &barrier_desc);
                        }

                        auto& backbuffer_rtv = backend.mSwapchain.getCurrentBackbufferRTV();
                        command_list->OMSetRenderTargets(1, &backbuffer_rtv, false, &depth_buffer_dsv.handle);

                        constexpr float clearColor[] = {0, 0, 0, 1};
                        command_list->ClearRenderTargetView(backbuffer_rtv, clearColor, 0, nullptr);
                        command_list->ClearDepthStencilView(depth_buffer_dsv.handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

                        // Mesh binding
                        command_list->IASetIndexBuffer(&mesh_ibv);
                        command_list->IASetVertexBuffers(0, 1, &mesh_vbv);
                        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


                        {
                            pass_data dpass;
                            dpass.albedo_tex = {mat_srv};

                            {
                                auto const proj = tg::perspective_directx(60_deg, window.getWidth() / float(window.getHeight()), 0.1f, 100.f);
                                auto target = tg::pos3(0, 1, 0);
                                auto camPos = tg::pos3(1, 1, 1) * 5.f;
                                auto view = tg::look_at_directx(camPos, target, tg::vec3(0, 1, 0));
                                dpass.view_proj = proj * view;
                            }

                            auto constexpr payload_index = 0;
                            auto payload = get_payload_data(dpass, payload_sizes[payload_index]);
                            root_sig.bind(backend.mDevice.getDevice(), *command_list, dynamicBufferRing, descManager, payload_index, payload);
                        }

                        for (auto modelpos : {tg::vec3(0, 0, 0), tg::vec3(3, 0, 0), tg::vec3(0, 3, 0), tg::vec3(0, 0, 3)})
                        {
                            instance_data dinst;
                            dinst.model = tg::translation(modelpos) * tg::rotation_y(tg::radians(-1 * float(run_time)));

                            auto constexpr payload_index = 1;
                            auto payload = get_payload_data(dinst, payload_sizes[payload_index]);
                            root_sig.bind(backend.mDevice.getDevice(), *command_list, dynamicBufferRing, descManager, payload_index, payload);

                            command_list->DrawIndexedInstanced(mesh_num_indices, 1, 0, 0, 0);
                        }

                        {
                            // transition backbuffer back to present state from render target state
                            auto const barrier_desc = CD3DX12_RESOURCE_BARRIER::Transition(
                                current_backbuffer_resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                            command_list->ResourceBarrier(1, &barrier_desc);
                        }

                        command_list->Close();

                        backend.mDirectQueue.submit(command_list);
                    }

                    backend.mSwapchain.present();
                }
            }

            // Cleanup before dtors get called
            backend.flushGPU();
            backend.mSwapchain.setFullscreen(false);
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

TEST("pr adapter choice", exclusive)
{
    auto constexpr mute = false;

#ifdef PR_BACKEND_D3D12
    if (0)
    {
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
#endif
}
