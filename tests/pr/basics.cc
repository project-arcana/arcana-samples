#include <array>
#include <iostream>
#include <nexus/test.hh>
#include <typed-geometry/tg.hh>

#ifdef PR_BACKEND_D3D12
#include <phantasm-renderer/backend/d3d12/BackendD3D12.hh>
#include <phantasm-renderer/backend/d3d12/CommandList.hh>
#include <phantasm-renderer/backend/d3d12/adapter_choice_util.hh>
#include <phantasm-renderer/backend/d3d12/common/d3dx12.hh>
#include <phantasm-renderer/backend/d3d12/common/util.hh>
#include <phantasm-renderer/backend/d3d12/device_tentative/timer.hh>
#include <phantasm-renderer/backend/d3d12/memory/DynamicBufferRing.hh>
#include <phantasm-renderer/backend/d3d12/memory/StaticBufferPool.hh>
#include <phantasm-renderer/backend/d3d12/memory/UploadHeap.hh>
#include <phantasm-renderer/backend/d3d12/pipeline_state.hh>
#include <phantasm-renderer/backend/d3d12/resources/resource_creation.hh>
#include <phantasm-renderer/backend/d3d12/resources/simple_vertex.hh>
#include <phantasm-renderer/backend/d3d12/root_signature.hh>
#include <phantasm-renderer/backend/d3d12/shader.hh>
#endif

#ifdef PR_BACKEND_VULKAN
#include <phantasm-renderer/backend/vulkan/BackendVulkan.hh>
#include <phantasm-renderer/backend/vulkan/common/verify.hh>
#include <phantasm-renderer/backend/vulkan/common/zero_struct.hh>
#include <phantasm-renderer/backend/vulkan/shader.hh>
#endif

#include <phantasm-renderer/backend/device_tentative/window.hh>
#include <phantasm-renderer/default_config.hh>


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
    if constexpr (0)
    {
        using namespace pr::backend;

        pr::backend::device::Window window;
        window.initialize("Liveness test");

        BackendD3D12 backend;
        {
            d3d12_config config;
            config.enable_validation_extended = true;
            backend.initialize(config, window.getHandle());
        }

        CommandListRing commandListRing;
        DescriptorAllocator descManager;
        DynamicBufferRing dynamicBufferRing;
        UploadHeap uploadHeap;
        {
            auto const num_backbuffers = backend.mSwapchain.getNumBackbuffers();
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

                uploadHeap.barrierResourceOnFlush(material.get_allocation(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

                uploadHeap.barrierResourceOnFlush(mesh_indices.get_allocation(), D3D12_RESOURCE_STATE_INDEX_BUFFER);
                uploadHeap.barrierResourceOnFlush(mesh_vertices.get_allocation(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
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

                    backend.mSwapchain.waitForBackbuffer();

                    // ... render to swapchain ...
                    {
                        auto* const command_list = commandListRing.acquireCommandList();

                        util::set_viewport(command_list, backend.mSwapchain.getBackbufferSize());
                        descManager.setHeaps(*command_list);
                        command_list->SetPipelineState(pso);
                        command_list->SetGraphicsRootSignature(root_sig.raw_root_sig);

                        backend.mSwapchain.barrierToRenderTarget(command_list);

                        auto backbuffer_rtv = backend.mSwapchain.getCurrentBackbufferRTV();
                        command_list->OMSetRenderTargets(1, &backbuffer_rtv, false, &depth_buffer_dsv.handle);

                        constexpr float clearColor[] = {0, 0, 0, 1};
                        command_list->ClearRenderTargetView(backbuffer_rtv, clearColor, 0, nullptr);
                        command_list->ClearDepthStencilView(depth_buffer_dsv.handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

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

                        backend.mSwapchain.barrierToPresent(command_list);

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
    {
        using namespace pr::backend;
        using namespace pr::backend::vk;

        device::Window window;
        window.initialize("Liveness test");
        window.pollEvents();

        vulkan_config config;
        config.enable_validation = true;
        BackendVulkan bv;
        bv.initialize(config, window);

        auto shader_pixel = create_shader_from_spirv_file(bv.mDevice.getDevice(), "testdata/frag.spv", shader_domain::pixel);
        auto shader_vertex = create_shader_from_spirv_file(bv.mDevice.getDevice(), "testdata/vert.spv", shader_domain::vertex);

        auto const backbufferExtent = VkExtent2D{unsigned(window.getWidth()), unsigned(window.getHeight())};

        VkRenderPass renderPass;
        VkPipelineLayout pipelineLayout;
        VkPipeline graphicsPipeline;
        VkViewport viewport = {};
        {
            cc::capped_vector<VkPipelineShaderStageCreateInfo, 4> stages;
            stages.push_back(shader_pixel.get_create_info());
            stages.push_back(shader_vertex.get_create_info());

            VkPipelineVertexInputStateCreateInfo vertex_input;
            zero_info_struct(vertex_input, VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
            vertex_input.vertexBindingDescriptionCount = 0;
            vertex_input.pVertexBindingDescriptions = nullptr; // Optional
            vertex_input.vertexAttributeDescriptionCount = 0;
            vertex_input.pVertexAttributeDescriptions = nullptr; // Optional

            VkPipelineInputAssemblyStateCreateInfo input_assembly;
            zero_info_struct(input_assembly, VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            input_assembly.primitiveRestartEnable = VK_FALSE;

            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = float(window.getWidth());
            viewport.height = float(window.getHeight());
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor = {};
            scissor.offset = {0, 0};
            scissor.extent = backbufferExtent;

            VkPipelineViewportStateCreateInfo viewportState;
            zero_info_struct(viewportState, VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizer = {};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
            rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;
            rasterizer.depthBiasConstantFactor = 0.0f; // Optional
            rasterizer.depthBiasClamp = 0.0f;          // Optional
            rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

            VkPipelineMultisampleStateCreateInfo multisampling = {};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            multisampling.minSampleShading = 1.0f;          // Optional
            multisampling.pSampleMask = nullptr;            // Optional
            multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
            multisampling.alphaToOneEnable = VK_FALSE;      // Optional

            VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;             // Optional
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;             // Optional

            VkPipelineColorBlendStateCreateInfo colorBlending = {};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;
            colorBlending.blendConstants[0] = 0.0f; // Optional
            colorBlending.blendConstants[1] = 0.0f; // Optional
            colorBlending.blendConstants[2] = 0.0f; // Optional
            colorBlending.blendConstants[3] = 0.0f; // Optional

            VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH};

            VkPipelineDynamicStateCreateInfo dynamicState = {};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 2;
            dynamicState.pDynamicStates = dynamicStates;

            VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 0;            // Optional
            pipelineLayoutInfo.pSetLayouts = nullptr;         // Optional
            pipelineLayoutInfo.pushConstantRangeCount = 0;    // Optional
            pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

            PR_VK_VERIFY_SUCCESS(vkCreatePipelineLayout(bv.mDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout));


            VkAttachmentDescription colorAttachment = {};
            colorAttachment.format = bv.mSwapchain.getBackbufferFormat();
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorAttachmentRef = {};
            colorAttachmentRef.attachment = 0;
            colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass = {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorAttachmentRef;

            VkSubpassDependency dependency = {};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = 1;
            renderPassInfo.pAttachments = &colorAttachment;
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpass;
            renderPassInfo.dependencyCount = 1;
            renderPassInfo.pDependencies = &dependency;

            PR_VK_VERIFY_SUCCESS(vkCreateRenderPass(bv.mDevice.getDevice(), &renderPassInfo, nullptr, &renderPass));


            VkGraphicsPipelineCreateInfo pipelineInfo = {};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = stages.size();
            pipelineInfo.pStages = stages.data();
            pipelineInfo.pVertexInputState = &vertex_input;
            pipelineInfo.pInputAssemblyState = &input_assembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = nullptr; // Optional
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = pipelineLayout;
            pipelineInfo.renderPass = renderPass;
            pipelineInfo.subpass = 0;
            pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
            pipelineInfo.basePipelineIndex = -1;              // Optional

            PR_VK_VERIFY_SUCCESS(vkCreateGraphicsPipelines(bv.mDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline));
        }

        cc::vector<VkFramebuffer> swapChainFramebuffers;
        {
            for (auto view : bv.mSwapchain.getBackbufferViews())
            {
                VkImageView attachments[] = {view};

                VkFramebufferCreateInfo framebufferInfo = {};
                framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferInfo.renderPass = renderPass;
                framebufferInfo.attachmentCount = 1;
                framebufferInfo.pAttachments = attachments;
                framebufferInfo.width = window.getWidth();
                framebufferInfo.height = window.getHeight();
                framebufferInfo.layers = 1;

                auto& new_framebuffer = swapChainFramebuffers.emplace_back();
                PR_VK_VERIFY_SUCCESS(vkCreateFramebuffer(bv.mDevice.getDevice(), &framebufferInfo, nullptr, &new_framebuffer));
            }
        }

        VkCommandPool commandPool;
        {
            VkCommandPoolCreateInfo poolInfo = {};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = bv.mDevice.getQueueFamilyGraphics();
            poolInfo.flags = 0; // Optional

            PR_VK_VERIFY_SUCCESS(vkCreateCommandPool(bv.mDevice.getDevice(), &poolInfo, nullptr, &commandPool));
        }

        cc::vector<VkCommandBuffer> commandBuffers;
        {
            commandBuffers.resize(bv.mSwapchain.getNumBackbuffers());

            VkCommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = uint32_t(commandBuffers.size());

            PR_VK_VERIFY_SUCCESS(vkAllocateCommandBuffers(bv.mDevice.getDevice(), &allocInfo, commandBuffers.data()));
        }

        for (auto i = 0u; i < commandBuffers.size(); ++i)
        {
            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = 0;                  // Optional
            beginInfo.pInheritanceInfo = nullptr; // Optional

            PR_VK_VERIFY_SUCCESS(vkBeginCommandBuffer(commandBuffers[i], &beginInfo));

            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = renderPass;
            renderPassInfo.framebuffer = swapChainFramebuffers[i];
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = backbufferExtent;

            VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearColor;

            vkCmdSetViewport(commandBuffers[i], 0, 1, &viewport);
            vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
            vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffers[i]);

            PR_VK_VERIFY_SUCCESS(vkEndCommandBuffer(commandBuffers[i]));
        }

        VkSemaphore imageAvailableSemaphore;
        VkSemaphore renderFinishedSemaphore;
        {
            VkSemaphoreCreateInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            PR_VK_VERIFY_SUCCESS(vkCreateSemaphore(bv.mDevice.getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphore));
            PR_VK_VERIFY_SUCCESS(vkCreateSemaphore(bv.mDevice.getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphore));
        }

        auto const on_resize_func = [&](int w, int h) {
            viewport.width = w;
            viewport.height = h;
            std::cout << "resize to " << w << "x" << h << std::endl;
        };

        while (!window.isRequestingClose())
        {
            window.pollEvents();
            if (window.isPendingResize())
            {
                if (!window.isMinimized())
                {
                    on_resize_func(window.getWidth(), window.getHeight());
                }
                window.clearPendingResize();
            }

            if (!window.isMinimized())
            {
                uint32_t imageIndex;
                vkAcquireNextImageKHR(bv.mDevice.getDevice(), bv.mSwapchain.getSwapchain(), UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

                VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
                VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
                submitInfo.waitSemaphoreCount = 1;
                submitInfo.pWaitSemaphores = waitSemaphores;
                submitInfo.pWaitDstStageMask = waitStages;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

                VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = signalSemaphores;

                PR_VK_VERIFY_SUCCESS(vkQueueSubmit(bv.mDevice.getQueueGraphics(), 1, &submitInfo, VK_NULL_HANDLE));

                {
                    VkPresentInfoKHR presentInfo = {};
                    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

                    presentInfo.waitSemaphoreCount = 1;
                    presentInfo.pWaitSemaphores = signalSemaphores;

                    VkSwapchainKHR swapChains[] = {bv.mSwapchain.getSwapchain()};
                    presentInfo.swapchainCount = 1;
                    presentInfo.pSwapchains = swapChains;
                    presentInfo.pImageIndices = &imageIndex;
                    presentInfo.pResults = nullptr; // Optional

                    vkQueuePresentKHR(bv.mDevice.getQueueGraphics(), &presentInfo);
                    vkQueueWaitIdle(bv.mDevice.getQueueGraphics());
                }
            }
        }
        {
            auto const& device = bv.mDevice.getDevice();

            vkDeviceWaitIdle(device);

            vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
            vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);

            for (auto framebuffer : swapChainFramebuffers)
            {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }

            vkDestroyCommandPool(device, commandPool, nullptr);
            vkDestroyPipeline(device, graphicsPipeline, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyRenderPass(device, renderPass, nullptr);

            shader_pixel.free(device);
            shader_vertex.free(device);
        }
    }
#endif
}

TEST("pr adapter choice", exclusive)
{
#ifdef PR_BACKEND_D3D12
    auto constexpr mute = false;
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
