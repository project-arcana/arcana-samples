#include <nexus/test.hh>

#include <array>
#include <iostream>

#include <typed-geometry/tg.hh>

#include <clean-core/capped_vector.hh>

#ifdef PR_BACKEND_D3D12
#include <phantasm-renderer/backend/d3d12/BackendD3D12.hh>
#include <phantasm-renderer/backend/d3d12/CommandList.hh>
#include <phantasm-renderer/backend/d3d12/adapter_choice_util.hh>
#include <phantasm-renderer/backend/d3d12/common/d3dx12.hh>
#include <phantasm-renderer/backend/d3d12/common/util.hh>
#include <phantasm-renderer/backend/d3d12/memory/DynamicBufferRing.hh>
#include <phantasm-renderer/backend/d3d12/memory/StaticBufferPool.hh>
#include <phantasm-renderer/backend/d3d12/memory/UploadHeap.hh>
#include <phantasm-renderer/backend/d3d12/pipeline_state.hh>
#include <phantasm-renderer/backend/d3d12/resources/resource_creation.hh>
#include <phantasm-renderer/backend/d3d12/resources/vertex_attributes.hh>
#include <phantasm-renderer/backend/d3d12/root_signature.hh>
#include <phantasm-renderer/backend/d3d12/shader.hh>
#endif

#ifdef PR_BACKEND_VULKAN
#include <phantasm-renderer/backend/vulkan/BackendVulkan.hh>
#include <phantasm-renderer/backend/vulkan/CommandBufferRing.hh>
#include <phantasm-renderer/backend/vulkan/common/util.hh>
#include <phantasm-renderer/backend/vulkan/common/verify.hh>
#include <phantasm-renderer/backend/vulkan/common/zero_struct.hh>
#include <phantasm-renderer/backend/vulkan/memory/Allocator.hh>
#include <phantasm-renderer/backend/vulkan/memory/DynamicBufferRing.hh>
#include <phantasm-renderer/backend/vulkan/memory/UploadHeap.hh>
#include <phantasm-renderer/backend/vulkan/memory/VMA.hh>
#include <phantasm-renderer/backend/vulkan/pipeline_state.hh>
#include <phantasm-renderer/backend/vulkan/resources/resource_creation.hh>
#include <phantasm-renderer/backend/vulkan/resources/resource_state.hh>
#include <phantasm-renderer/backend/vulkan/resources/resource_view.hh>
#include <phantasm-renderer/backend/vulkan/resources/vertex_attributes.hh>
#include <phantasm-renderer/backend/vulkan/shader.hh>
#endif

#include <phantasm-renderer/backend/assets/image_loader.hh>
#include <phantasm-renderer/backend/assets/mesh_loader.hh>
#include <phantasm-renderer/backend/device_tentative/timer.hh>
#include <phantasm-renderer/backend/device_tentative/window.hh>
#include <phantasm-renderer/default_config.hh>


namespace
{
auto const get_projection_matrix = [](int w, int h) -> tg::mat4 { return tg::perspective_directx(60_deg, w / float(h), 0.1f, 100.f); };

auto const get_view_matrix = []() -> tg::mat4 {
    constexpr auto target = tg::pos3(0, 1, 0);
    constexpr auto cam_pos = tg::pos3(1, 1, 1) * 5.f;
    return tg::look_at_directx(cam_pos, target, tg::vec3(0, 1, 0));
};

auto const get_model_matrix
    = [](tg::vec3 pos, double runtime) -> tg::mat4 { return tg::translation(pos) * tg::rotation_y(tg::radians(float(runtime))); };

constexpr auto sample_mesh_path = "testdata/mesh/apollo.obj";
constexpr auto sample_texture_path = "testdata/texture/uv_checker.png";
}

#ifdef PR_BACKEND_D3D12
namespace
{
struct model_matrix_data
{
    static constexpr auto num_instances = 4;

    struct padded_instance
    {
        tg::mat4 model_mat;
        char padding[256 - sizeof(tg::mat4)];
    };
    static_assert(sizeof(padded_instance) == 256);

    cc::array<padded_instance, num_instances> model_matrices;
};
static_assert(sizeof(model_matrix_data) == sizeof(model_matrix_data::padded_instance) * model_matrix_data::num_instances);
}
#endif

TEST("pr backend liveness", exclusive)
{
#ifdef PR_BACKEND_D3D12
    (void)0;
    if (0)
    {
        using namespace pr::backend;
        using namespace pr::backend::d3d12;

        pr::backend::device::Window window;
        window.initialize("Liveness test | D3D12");

        BackendD3D12 backend;
        {
            d3d12_config config;
            config.enable_validation_extended = true;
            backend.initialize(config, window.getHandle());
        }

        CommandListRing commandListRing;
        DescriptorAllocator descAllocator;
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
            descAllocator.initialize(backend.mDevice.getDevice(), num_shader_resources, num_dsvs, num_rtvs, num_samplers, num_backbuffers);
            dynamicBufferRing.initialize(backend.mDevice.getDevice(), num_backbuffers, dynamic_buffer_size);
            uploadHeap.initialize(&backend, upload_size);
        }

        {
            // Shader compilation
            //
            cc::capped_vector<shader, 6> shaders;
            {
                shaders.push_back(load_binary_shader_from_file("testdata/shader/dxil/vertex.dxil", shader_domain::vertex));
                shaders.push_back(load_binary_shader_from_file("testdata/shader/dxil/pixel.dxil", shader_domain::pixel));

                for (auto const& s : shaders)
                    CC_RUNTIME_ASSERT(s.is_valid() && "failed to load shaders");
            }

            // Resource setup
            //
            resource material;
            cpu_cbv_srv_uav mat_srv = descAllocator.allocCBV_SRV_UAV();
            {
                material = create_texture2d_from_file(backend.mAllocator, backend.mDevice.getDevice(), uploadHeap, sample_texture_path);
                make_srv(material, mat_srv.handle);

                uploadHeap.barrierResourceOnFlush(material.get_allocation(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }

            resource mesh_vertices;
            resource mesh_indices;
            D3D12_VERTEX_BUFFER_VIEW mesh_vbv;
            D3D12_INDEX_BUFFER_VIEW mesh_ibv;
            unsigned mesh_num_indices = 0;
            {
                auto const mesh_data = assets::load_obj_mesh(sample_mesh_path);
                mesh_num_indices = unsigned(mesh_data.indices.size());

                mesh_indices = create_buffer_from_data<int>(backend.mAllocator, uploadHeap, mesh_data.indices);
                mesh_vertices = create_buffer_from_data<assets::simple_vertex>(backend.mAllocator, uploadHeap, mesh_data.vertices);

                mesh_ibv = make_index_buffer_view(mesh_indices, sizeof(int));
                mesh_vbv = make_vertex_buffer_view(mesh_vertices, sizeof(assets::simple_vertex));

                uploadHeap.barrierResourceOnFlush(mesh_indices.get_allocation(), D3D12_RESOURCE_STATE_INDEX_BUFFER);
                uploadHeap.barrierResourceOnFlush(mesh_vertices.get_allocation(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            }

            uploadHeap.flushAndFinish();

            root_signature root_sig;
            {
                shader_payload_shape payload_shape;

                // Argument 0, camera CBV
                {
                    shader_payload_shape::shader_argument_shape arg_shape;
                    arg_shape.has_cb = true;
                    arg_shape.num_srvs = 0;
                    arg_shape.num_uavs = 0;
                    payload_shape.shader_arguments.push_back(arg_shape);
                }

                // Argument 1, pixel shader SRV and model matrix CBV
                {
                    shader_payload_shape::shader_argument_shape arg_shape;
                    arg_shape.has_cb = true;
                    arg_shape.num_srvs = 1;
                    arg_shape.num_uavs = 0;
                    payload_shape.shader_arguments.push_back(arg_shape);
                }

                root_sig.initialize(backend.mDevice.getDevice(), payload_shape);
            }

            resource depth_buffer;
            cpu_dsv depth_buffer_dsv = descAllocator.allocDSV();

            resource color_buffer;
            cpu_rtv color_buffer_rtv = descAllocator.allocRTV();

            shared_com_ptr<ID3D12PipelineState> pso;
            {
                wip::framebuffer_format framebuf;
                framebuf.depth_target.push_back(DXGI_FORMAT_D32_FLOAT);
                framebuf.render_targets.push_back(backend.mSwapchain.getBackbufferFormat());

                auto const input_layout = get_vertex_attributes<assets::simple_vertex>();
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
            device::Timer timer;
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
                    timer.reset();
                    run_time += frametime;

                    dynamicBufferRing.onBeginFrame();
                    descAllocator.onBeginFrame();
                    commandListRing.onBeginFrame();


                    // ... do something else ...

                    backend.mSwapchain.waitForBackbuffer();

                    // ... render to swapchain ...
                    {
                        auto* const command_list = commandListRing.acquireCommandList();

                        util::set_viewport(command_list, backend.mSwapchain.getBackbufferSize());
                        descAllocator.setHeaps(*command_list);
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
                            struct vert_cb
                            {
                                tg::mat4 view_proj;
                            };

                            vert_cb* vert_cb_data;
                            D3D12_GPU_VIRTUAL_ADDRESS vert_cb_va;
                            dynamicBufferRing.allocConstantBufferTyped(vert_cb_data, vert_cb_va);

                            vert_cb_data->view_proj = get_projection_matrix(window.getWidth(), window.getHeight()) * get_view_matrix();

                            shader_argument arg_zero;
                            arg_zero.cbv_view_offset = 0;
                            arg_zero.cbv_va = vert_cb_va;

                            root_sig.bind(backend.mDevice.getDevice(), *command_list, descAllocator, 0, arg_zero);
                        }

                        {
                            model_matrix_data* vert_cb_data;
                            D3D12_GPU_VIRTUAL_ADDRESS vert_cb_va;
                            dynamicBufferRing.allocConstantBufferTyped(vert_cb_data, vert_cb_va);

                            // record model matrices
                            auto index = 0u;
                            for (auto modelpos : {tg::vec3(0, 0, 0), tg::vec3(3, 0, 0), tg::vec3(0, 3, 0), tg::vec3(0, 0, 3)})
                            {
                                vert_cb_data->model_matrices[index++].model_mat = get_model_matrix(modelpos, run_time);
                            }

                            for (auto i = 0u; i < model_matrix_data::num_instances; ++i)
                            {
                                shader_argument arg_one;
                                arg_one.cbv_view_offset = i * sizeof(vert_cb_data->model_matrices[0]);
                                arg_one.cbv_va = vert_cb_va;
                                arg_one.srvs.push_back(mat_srv);

                                root_sig.bind(backend.mDevice.getDevice(), *command_list, descAllocator, 1, arg_one);
                                command_list->DrawIndexedInstanced(mesh_num_indices, 1, 0, 0, 0);
                            }
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
    if (10)
    {
        using namespace pr::backend;
        using namespace pr::backend::vk;

        device::Window window;
        window.initialize("Liveness test | Vulkan");

        BackendVulkan bv;
        {
            vulkan_config config;
            config.enable_validation = true;
            bv.initialize(config, window);
        }

        CommandBufferRing commandBufferRing;
        DynamicBufferRing dynamicBufferRing;
        UploadHeap uploadHeap;
        DescriptorAllocator descAllocator;
        {
            auto const num_backbuffers = bv.mSwapchain.getNumBackbuffers();
            auto const num_command_lists = 8;
            auto const num_cbvs = 2000;
            auto const num_srvs = 2000;
            auto const num_uavs = 20;
            //            auto const num_dsvs = 3;
            //            auto const num_rtvs = 60;
            auto const num_samplers = 20;
            auto const dynamic_buffer_size = 20 * 1024 * 1024;
            auto const upload_size = 1000 * 1024 * 1024;

            commandBufferRing.initialize(bv.mDevice, num_backbuffers, num_command_lists, bv.mDevice.getQueueFamilyGraphics());
            dynamicBufferRing.initialize(&bv.mDevice, &bv.mAllocator, num_backbuffers, dynamic_buffer_size);
            uploadHeap.initialize(&bv.mDevice, upload_size);
            descAllocator.initialize(&bv.mDevice, num_cbvs, num_srvs, num_uavs, num_samplers);
        }


        VkDescriptorSetLayout arcDescriptorSetLayout;
        {
            cc::capped_vector<VkDescriptorSetLayoutBinding, 8> bindings;
            {
                VkDescriptorSetLayoutBinding& binding = bindings.emplace_back();
                binding = {};
                binding.binding = uint32_t(bindings.size() - 1);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                binding.descriptorCount = 1;
                binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                binding.pImmutableSamplers = nullptr; // Optional
            }
            {
                VkDescriptorSetLayoutBinding& binding = bindings.emplace_back();
                binding = {};
                binding.binding = uint32_t(bindings.size() - 1);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                binding.descriptorCount = 1;
                binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                binding.pImmutableSamplers = nullptr; // Optional
            }

            VkDescriptorSetLayoutCreateInfo layoutInfo = {};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = uint32_t(bindings.size());
            layoutInfo.pBindings = bindings.data();


            PR_VK_VERIFY_SUCCESS(vkCreateDescriptorSetLayout(bv.mDevice.getDevice(), &layoutInfo, nullptr, &arcDescriptorSetLayout));
        }

        VkDescriptorSetLayout presentDescriptorSetLayout;
        {
            cc::capped_vector<VkDescriptorSetLayoutBinding, 8> bindings;
            {
                VkDescriptorSetLayoutBinding& binding = bindings.emplace_back();
                binding = {};
                binding.binding = uint32_t(bindings.size() - 1);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                binding.descriptorCount = 1;
                binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                binding.pImmutableSamplers = nullptr; // Optional
            }

            VkDescriptorSetLayoutCreateInfo layoutInfo = {};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = uint32_t(bindings.size());
            layoutInfo.pBindings = bindings.data();


            PR_VK_VERIFY_SUCCESS(vkCreateDescriptorSetLayout(bv.mDevice.getDevice(), &layoutInfo, nullptr, &presentDescriptorSetLayout));
        }

        VkPipelineLayout arcPipelineLayout;
        {
            VkPushConstantRange pushConstantRange = {};
            pushConstantRange.size = sizeof(tg::mat4);
            pushConstantRange.offset = 0;
            pushConstantRange.stageFlags = to_shader_stage_flags(shader_domain::vertex);

            VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;                       // Optional
            pipelineLayoutInfo.pSetLayouts = &arcDescriptorSetLayout;    // Optional
            pipelineLayoutInfo.pushConstantRangeCount = 1;               // Optional
            pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange; // Optional

            PR_VK_VERIFY_SUCCESS(vkCreatePipelineLayout(bv.mDevice.getDevice(), &pipelineLayoutInfo, nullptr, &arcPipelineLayout));
        }

        VkPipelineLayout presentPipelineLayout;
        {
            VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;                        // Optional
            pipelineLayoutInfo.pSetLayouts = &presentDescriptorSetLayout; // Optional
            pipelineLayoutInfo.pushConstantRangeCount = 0;                // Optional
            pipelineLayoutInfo.pPushConstantRanges = nullptr;             // Optional

            PR_VK_VERIFY_SUCCESS(vkCreatePipelineLayout(bv.mDevice.getDevice(), &pipelineLayoutInfo, nullptr, &presentPipelineLayout));
        }

        image depth_image;
        image color_rt_image;
        VkImageView depth_view;
        VkImageView color_rt_view;
        VkSampler color_rt_sampler;
        {
            depth_image = create_depth_stencil(bv.mAllocator, 15u, 15u, VK_FORMAT_D32_SFLOAT);
            depth_view = make_image_view(bv.mDevice.getDevice(), depth_image, VK_FORMAT_D32_SFLOAT);

            color_rt_image = create_render_target(bv.mAllocator, 15u, 15u, VK_FORMAT_R16G16B16A16_SFLOAT);
            color_rt_view = make_image_view(bv.mDevice.getDevice(), color_rt_image, VK_FORMAT_R16G16B16A16_SFLOAT);

            {
                VkSamplerCreateInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                info.magFilter = VK_FILTER_NEAREST;
                info.minFilter = VK_FILTER_NEAREST;
                info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                info.minLod = -1000;
                info.maxLod = 1000;
                info.maxAnisotropy = 1.0f;
                vkCreateSampler(bv.mDevice.getDevice(), &info, nullptr, &color_rt_sampler);
            }
        }

        VkRenderPass arcRenderPass;
        {
            wip::framebuffer_format framebuffer_format;
            framebuffer_format.render_targets.push_back(VK_FORMAT_R16G16B16A16_SFLOAT);
            framebuffer_format.depth_target.push_back(VK_FORMAT_D32_SFLOAT);

            auto arc_prim_config = pr::primitive_pipeline_config{};
            arc_prim_config.cull = pr::cull_mode::none;
            arcRenderPass = create_render_pass(bv.mDevice.getDevice(), framebuffer_format, arc_prim_config);
        }

        cc::capped_vector<shader, 6> arcShaders;
        arcShaders.push_back(create_shader_from_spirv_file(bv.mDevice.getDevice(), "testdata/shader/spirv/frag.spv", shader_domain::pixel, "main"));
        arcShaders.push_back(create_shader_from_spirv_file(bv.mDevice.getDevice(), "testdata/shader/spirv/vert.spv", shader_domain::vertex, "main"));

        VkPipeline arcPipeline;
        VkFramebuffer arcFramebuffer;
        {
            auto const vert_attribs = get_vertex_attributes<assets::simple_vertex>();
            arcPipeline = create_pipeline(bv.mDevice.getDevice(), arcRenderPass, arcPipelineLayout, arcShaders, pr::default_config, vert_attribs,
                                          sizeof(assets::simple_vertex));

            {
                cc::array const attachments = {color_rt_view, depth_view};
                VkFramebufferCreateInfo fb_info;
                zero_info_struct(fb_info, VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
                fb_info.pNext = nullptr;
                fb_info.renderPass = arcRenderPass;
                fb_info.attachmentCount = unsigned(attachments.size());
                fb_info.pAttachments = attachments.data();
                fb_info.width = unsigned(15);
                fb_info.height = unsigned(15);
                fb_info.layers = 1;
                PR_VK_VERIFY_SUCCESS(vkCreateFramebuffer(bv.mDevice.getDevice(), &fb_info, nullptr, &arcFramebuffer));
            }
        }

        cc::capped_vector<shader, 6> presentShaders;
        presentShaders.push_back(create_shader_from_spirv_file(bv.mDevice.getDevice(), "testdata/shader/spirv/fs_blit_frag.spv", shader_domain::pixel, "main"));
        presentShaders.push_back(create_shader_from_spirv_file(bv.mDevice.getDevice(), "testdata/shader/spirv/fs_vert.spv", shader_domain::vertex, "main"));

        VkPipeline presentPipeline;
        {
            presentPipeline = create_fullscreen_pipeline(bv.mDevice.getDevice(), bv.mSwapchain.getRenderPass(), presentPipelineLayout, presentShaders);
        }

        buffer vert_buf;
        buffer ind_buf;
        unsigned num_indices;

        {
            auto const mesh_data = assets::load_obj_mesh(sample_mesh_path);
            num_indices = unsigned(mesh_data.indices.size());

            vert_buf = create_vertex_buffer_from_data<assets::simple_vertex>(bv.mAllocator, uploadHeap, mesh_data.vertices);
            ind_buf = create_index_buffer_from_data<int>(bv.mAllocator, uploadHeap, mesh_data.indices);
        }

        image albedo_image;
        VkImageView albedo_view;
        VkSampler albedo_sampler;
        {
            albedo_image = create_texture_from_file(bv.mAllocator, uploadHeap, sample_texture_path);
            albedo_view = make_image_view(bv.mDevice.getDevice(), albedo_image, VK_FORMAT_R8G8B8A8_UNORM);

            {
                VkSamplerCreateInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                info.magFilter = VK_FILTER_LINEAR;
                info.minFilter = VK_FILTER_LINEAR;
                info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                info.minLod = -1000;
                info.maxLod = 1000;
                info.maxAnisotropy = 1.0f;
                vkCreateSampler(bv.mDevice.getDevice(), &info, nullptr, &albedo_sampler);
            }
        }

        uploadHeap.flushAndFinish();

        cc::capped_array<VkDescriptorSet, 6> arcDescriptorSets;
        {
            arcDescriptorSets.emplace(bv.mSwapchain.getNumBackbuffers());
            for (auto& set : arcDescriptorSets)
            {
                descAllocator.allocDescriptor(arcDescriptorSetLayout, set);

                // update descriptor set, image and sample part
                VkDescriptorImageInfo desc_image[1] = {};
                desc_image[0].sampler = albedo_sampler;
                desc_image[0].imageView = albedo_view;
                desc_image[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkWriteDescriptorSet writes[1];
                writes[0] = {};
                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].pNext = nullptr;
                writes[0].dstSet = set;
                writes[0].descriptorCount = 1;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[0].pImageInfo = desc_image;
                writes[0].dstArrayElement = 0;
                writes[0].dstBinding = 1;

                vkUpdateDescriptorSets(bv.mDevice.getDevice(), 1, writes, 0, nullptr);
            }
        }

        cc::capped_array<VkDescriptorSet, 6> presentDescriptorSets;
        {
            presentDescriptorSets.emplace(bv.mSwapchain.getNumBackbuffers());
            for (auto& set : presentDescriptorSets)
            {
                descAllocator.allocDescriptor(presentDescriptorSetLayout, set);
            }
        }

        auto const on_resize_func = [&](int w, int h) {
            bv.mSwapchain.onResize(w, h);

            // recreate depth buffer
            bv.mAllocator.free(depth_image);
            depth_image = create_depth_stencil(bv.mAllocator, unsigned(w), unsigned(h), VK_FORMAT_D32_SFLOAT);
            vkDestroyImageView(bv.mDevice.getDevice(), depth_view, nullptr);
            depth_view = make_image_view(bv.mDevice.getDevice(), depth_image, VK_FORMAT_D32_SFLOAT);

            // recreate color rt buffer
            bv.mAllocator.free(color_rt_image);
            color_rt_image = create_render_target(bv.mAllocator, unsigned(w), unsigned(h), VK_FORMAT_R16G16B16A16_SFLOAT);
            vkDestroyImageView(bv.mDevice.getDevice(), color_rt_view, nullptr);
            color_rt_view = make_image_view(bv.mDevice.getDevice(), color_rt_image, VK_FORMAT_R16G16B16A16_SFLOAT);

            // transition RTs
            {
                auto const cmd_buf = commandBufferRing.acquireCommandBuffer();

                barrier_bundle<2> barrier_dispatch;
                barrier_dispatch.add_image_barrier(color_rt_image.image,
                                                   state_change{resource_state::undefined, resource_state::shader_resource, shader_domain::pixel});
                barrier_dispatch.add_image_barrier(depth_image.image, state_change{resource_state::undefined, resource_state::depth_write}, true);
                barrier_dispatch.submit(cmd_buf, bv.mDevice.getQueueGraphics());
            }

            // recreate arc framebuffer
            {
                vkDestroyFramebuffer(bv.mDevice.getDevice(), arcFramebuffer, nullptr);
                cc::array const attachments = {color_rt_view, depth_view};
                VkFramebufferCreateInfo fb_info = {};
                fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fb_info.pNext = nullptr;
                fb_info.renderPass = arcRenderPass;
                fb_info.attachmentCount = unsigned(attachments.size());
                fb_info.pAttachments = attachments.data();
                fb_info.width = unsigned(w);
                fb_info.height = unsigned(h);
                fb_info.layers = 1;
                PR_VK_VERIFY_SUCCESS(vkCreateFramebuffer(bv.mDevice.getDevice(), &fb_info, nullptr, &arcFramebuffer));
            }

            // flush
            vkDeviceWaitIdle(bv.mDevice.getDevice());


            std::cout << "resize to " << w << "x" << h << std::endl;
        };

        device::Timer timer;
        double run_time = 0.0;

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
                auto const frametime = timer.getElapsedTime();
                timer.reset();
                run_time += frametime;

                bv.mSwapchain.waitForBackbuffer();
                commandBufferRing.onBeginFrame();
                dynamicBufferRing.onBeginFrame();

                auto const backbuffer_size = bv.mSwapchain.getBackbufferSize();

                auto const cmd_buf = commandBufferRing.acquireCommandBuffer();

                {
                    // transition color_rt to render target
                    barrier_bundle<1> barrier;
                    barrier.add_image_barrier(color_rt_image.image,
                                              state_change{resource_state::shader_resource, shader_domain::pixel, resource_state::render_target});
                    barrier.record(cmd_buf);
                }

                // Arc render pass
                {
                    VkRenderPassBeginInfo renderPassInfo = {};
                    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    renderPassInfo.renderPass = arcRenderPass;
                    renderPassInfo.framebuffer = arcFramebuffer;
                    renderPassInfo.renderArea.offset = {0, 0};
                    renderPassInfo.renderArea.extent.width = backbuffer_size.x;
                    renderPassInfo.renderArea.extent.height = backbuffer_size.y;

                    cc::array<VkClearValue, 2> clear_values;
                    clear_values[0] = {0.f, 0.f, 0.f, 1.f};
                    clear_values[1] = {1.f, 0};

                    renderPassInfo.clearValueCount = clear_values.size();
                    renderPassInfo.pClearValues = clear_values.data();

                    pr::backend::vk::util::set_viewport(cmd_buf, backbuffer_size);
                    vkCmdBeginRenderPass(cmd_buf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, arcPipeline);

                    VkBuffer vertex_buffers[] = {vert_buf.buffer};
                    VkDeviceSize offsets[] = {0};

                    vkCmdBindVertexBuffers(cmd_buf, 0, 1, vertex_buffers, offsets);
                    vkCmdBindIndexBuffer(cmd_buf, ind_buf.buffer, 0, VK_INDEX_TYPE_UINT32);


                    {
                        auto& descriptor_set = arcDescriptorSets[bv.mSwapchain.getCurrentBackbufferIndex()];
                        VkDescriptorBufferInfo descriptor_upload_info;
                        {
                            struct mvp_data
                            {
                                tg::mat4 vp;
                            };

                            {
                                mvp_data* data;
                                dynamicBufferRing.allocConstantBufferTyped(data, descriptor_upload_info);
                                data->vp = get_projection_matrix(window.getWidth(), window.getHeight()) * get_view_matrix();
                            }

                            dynamicBufferRing.setDescriptorSet(0, sizeof(mvp_data), descriptor_set);
                        }

                        cc::array const uniform_offsets = {uint32_t(descriptor_upload_info.offset)};
                        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, arcPipelineLayout, 0, 1, &descriptor_set,
                                                uint32_t(uniform_offsets.size()), uniform_offsets.data());
                    }

                    for (auto modelpos : {tg::vec3(0, 0, 0), tg::vec3(3, 0, 0), tg::vec3(0, 3, 0), tg::vec3(0, 0, 3)})
                    {
                        auto const model_mat = get_model_matrix(modelpos, run_time);
                        vkCmdPushConstants(cmd_buf, arcPipelineLayout, to_shader_stage_flags(shader_domain::vertex), 0, sizeof(tg::mat4), tg::data_ptr(model_mat));
                        vkCmdDrawIndexed(cmd_buf, num_indices, 1, 0, 0, 0);
                    }


                    vkCmdEndRenderPass(cmd_buf);
                }

                {
                    // transition color_rt to shader resource
                    barrier_bundle<1> barrier;
                    barrier.add_image_barrier(color_rt_image.image,
                                              state_change{resource_state::render_target, resource_state::shader_resource, shader_domain::pixel});
                    barrier.record(cmd_buf);
                }

                // Present render pass
                {
                    VkRenderPassBeginInfo renderPassInfo = {};
                    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    renderPassInfo.renderPass = bv.mSwapchain.getRenderPass();
                    renderPassInfo.framebuffer = bv.mSwapchain.getCurrentFramebuffer();
                    renderPassInfo.renderArea.offset = {0, 0};
                    renderPassInfo.renderArea.extent.width = backbuffer_size.x;
                    renderPassInfo.renderArea.extent.height = backbuffer_size.y;

                    cc::array<VkClearValue, 1> clear_values;
                    clear_values[0] = {0.f, 0.f, 0.f, 1.f};

                    renderPassInfo.clearValueCount = clear_values.size();
                    renderPassInfo.pClearValues = clear_values.data();

                    pr::backend::vk::util::set_viewport(cmd_buf, backbuffer_size);
                    vkCmdBeginRenderPass(cmd_buf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipeline);

                    {
                        auto& descriptor_set = presentDescriptorSets[bv.mSwapchain.getCurrentBackbufferIndex()];

                        // update srv and sampler
                        VkDescriptorImageInfo desc_image[1] = {};
                        desc_image[0].sampler = color_rt_sampler;
                        desc_image[0].imageView = color_rt_view;
                        desc_image[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                        VkWriteDescriptorSet writes[1];
                        writes[0] = {};
                        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        writes[0].pNext = nullptr;
                        writes[0].dstSet = descriptor_set;
                        writes[0].descriptorCount = 1;
                        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        writes[0].pImageInfo = desc_image;
                        writes[0].dstArrayElement = 0;
                        writes[0].dstBinding = 0;

                        vkUpdateDescriptorSets(bv.mDevice.getDevice(), 1, writes, 0, nullptr);


                        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipelineLayout, 0, 1, &descriptor_set, 0, nullptr);
                    }

                    vkCmdDraw(cmd_buf, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd_buf);
                }

                PR_VK_VERIFY_SUCCESS(vkEndCommandBuffer(cmd_buf));

                bv.mSwapchain.performPresentSubmit(cmd_buf);
                bv.mSwapchain.present();
            }
        }

        {
            auto const& device = bv.mDevice.getDevice();

            vkDeviceWaitIdle(device);

            for (auto& set : arcDescriptorSets)
                descAllocator.free(set);

            for (auto& set : presentDescriptorSets)
                descAllocator.free(set);

            bv.mAllocator.free(vert_buf);
            bv.mAllocator.free(ind_buf);

            bv.mAllocator.free(albedo_image);
            vkDestroySampler(device, albedo_sampler, nullptr);
            vkDestroyImageView(device, albedo_view, nullptr);

            bv.mAllocator.free(depth_image);
            vkDestroyImageView(device, depth_view, nullptr);

            bv.mAllocator.free(color_rt_image);
            vkDestroyImageView(device, color_rt_view, nullptr);
            vkDestroySampler(device, color_rt_sampler, nullptr);


            vkDestroyDescriptorSetLayout(device, arcDescriptorSetLayout, nullptr);
            vkDestroyFramebuffer(device, arcFramebuffer, nullptr);
            vkDestroyPipeline(device, arcPipeline, nullptr);
            vkDestroyRenderPass(device, arcRenderPass, nullptr);
            vkDestroyPipelineLayout(device, arcPipelineLayout, nullptr);

            vkDestroyDescriptorSetLayout(device, presentDescriptorSetLayout, nullptr);
            vkDestroyPipeline(device, presentPipeline, nullptr);
            vkDestroyPipelineLayout(device, presentPipelineLayout, nullptr);

            for (auto& shader : arcShaders)
                shader.free(device);

            for (auto& shader : presentShaders)
                shader.free(device);
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
#endif
}
