#ifdef PR_BACKEND_VULKAN
#include <nexus/test.hh>

#include "backend_common.hh"

#include <phantasm-renderer/backend/detail/unique_buffer.hh>

#include <phantasm-renderer/backend/vulkan/BackendVulkan.hh>
#include <phantasm-renderer/backend/vulkan/CommandBufferRing.hh>
#include <phantasm-renderer/backend/vulkan/common/util.hh>
#include <phantasm-renderer/backend/vulkan/common/verify.hh>
#include <phantasm-renderer/backend/vulkan/common/zero_struct.hh>
#include <phantasm-renderer/backend/vulkan/loader/spirv_patch_util.hh>
#include <phantasm-renderer/backend/vulkan/memory/DynamicBufferRing.hh>
#include <phantasm-renderer/backend/vulkan/memory/ResourceAllocator.hh>
#include <phantasm-renderer/backend/vulkan/memory/UploadHeap.hh>
#include <phantasm-renderer/backend/vulkan/pipeline_state.hh>
#include <phantasm-renderer/backend/vulkan/resources/resource_creation.hh>
#include <phantasm-renderer/backend/vulkan/resources/resource_state.hh>
#include <phantasm-renderer/backend/vulkan/resources/resource_view.hh>
#include <phantasm-renderer/backend/vulkan/shader.hh>
#include <phantasm-renderer/backend/vulkan/shader_arguments.hh>

TEST("pr::backend::vk liveness", exclusive)
{
    using namespace pr::backend;
    using namespace pr::backend::vk;

    device::Window window;
    window.initialize("pr::backend::vk liveness");

    BackendVulkan bv;
    bv.initialize(pr_test::get_backend_config(), window);

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
        auto const num_samplers = 20;
        auto const dynamic_buffer_size = 20 * 1024 * 1024;
        auto const upload_size = 1000 * 1024 * 1024;

        commandBufferRing.initialize(bv.mDevice, num_backbuffers, num_command_lists, bv.mDevice.getQueueFamilyGraphics());
        dynamicBufferRing.initialize(&bv.mDevice, &bv.mAllocator, num_backbuffers, dynamic_buffer_size);
        uploadHeap.initialize(&bv.mDevice, upload_size);
        descAllocator.initialize(bv.mDevice.getDevice(), num_cbvs, num_srvs, num_uavs, num_samplers);
    }

    pipeline_layout arc_pipeline_layout;
    {
        cc::capped_vector<arg::shader_argument_shape, 4> payload_shape;
        {
            // Argument 0, camera CBV
            {
                arg::shader_argument_shape arg_shape;
                arg_shape.has_cb = true;
                arg_shape.num_srvs = 0;
                arg_shape.num_uavs = 0;
                payload_shape.push_back(arg_shape);
            }

            // Argument 1, pixel shader SRV and model matrix CBV
            {
                arg::shader_argument_shape arg_shape;
                arg_shape.has_cb = true;
                arg_shape.num_srvs = 1;
                arg_shape.num_uavs = 0;
                payload_shape.push_back(arg_shape);
            }
        }

        arc_pipeline_layout.initialize(bv.mDevice.getDevice(), payload_shape);
    }

    pipeline_layout present_pipeline_layout;
    {
        cc::capped_vector<arg::shader_argument_shape, 4> payload_shape;
        {
            // Argument 0, blit target SRV
            {
                arg::shader_argument_shape arg_shape;
                arg_shape.has_cb = false;
                arg_shape.num_srvs = 1;
                arg_shape.num_uavs = 0;
                payload_shape.push_back(arg_shape);
            }
        }

        present_pipeline_layout.initialize(bv.mDevice.getDevice(), payload_shape);
    }

    image depth_image;
    image color_rt_image;
    VkImageView depth_view;
    VkImageView color_rt_view;
    {
        depth_image = create_depth_stencil(bv.mAllocator, 15u, 15u, VK_FORMAT_D32_SFLOAT);
        depth_view = make_image_view(bv.mDevice.getDevice(), depth_image, VK_FORMAT_D32_SFLOAT);

        color_rt_image = create_render_target(bv.mAllocator, 15u, 15u, VK_FORMAT_R16G16B16A16_SFLOAT);
        color_rt_view = make_image_view(bv.mDevice.getDevice(), color_rt_image, VK_FORMAT_R16G16B16A16_SFLOAT);
    }

    VkRenderPass arcRenderPass;
    {
        cc::capped_vector<format, 8> rtv_formats;
        rtv_formats.push_back(format::rgba16f);
        format const dsv_format = format::depth32f;

        auto arc_prim_config = pr::primitive_pipeline_config{};
        arcRenderPass = create_render_pass(bv.mDevice.getDevice(), arg::framebuffer_format{rtv_formats, cc::span{dsv_format}}, arc_prim_config);
    }

    VkPipeline arcPipeline;
    VkFramebuffer arcFramebuffer;
    {
        auto const vertex_binary = util::create_patched_spirv_from_binary_file("res/pr/liveness_sample/shader/spirv/vertex.spv");
        auto const pixel_binary = util::create_patched_spirv_from_binary_file("res/pr/liveness_sample/shader/spirv/pixel.spv");
        CC_DEFER
        {
            util::free_patched_spirv(vertex_binary);
            util::free_patched_spirv(pixel_binary);
        };

        cc::capped_vector<arg::shader_stage, 6> shader_stages;
        shader_stages.push_back(arg::shader_stage{vertex_binary.bytecode, vertex_binary.bytecode_size, shader_domain::vertex});
        shader_stages.push_back(arg::shader_stage{pixel_binary.bytecode, pixel_binary.bytecode_size, shader_domain::pixel});

        auto const vert_attribs = assets::get_vertex_attributes<assets::simple_vertex>();
        auto const input_layout = util::get_native_vertex_format(vert_attribs);
        arcPipeline = create_pipeline(bv.mDevice.getDevice(), arcRenderPass, arc_pipeline_layout.raw_layout, shader_stages, pr::default_config,
                                      input_layout, sizeof(assets::simple_vertex));

        {
            cc::array const attachments = {color_rt_view, depth_view};
            VkFramebufferCreateInfo fb_info;
            zero_info_struct(fb_info, VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
            fb_info.pNext = nullptr;
            fb_info.renderPass = arcRenderPass;
            fb_info.attachmentCount = unsigned(attachments.size());
            fb_info.pAttachments = attachments.data();
            fb_info.width = 15;
            fb_info.height = 15;
            fb_info.layers = 1;
            PR_VK_VERIFY_SUCCESS(vkCreateFramebuffer(bv.mDevice.getDevice(), &fb_info, nullptr, &arcFramebuffer));
        }
    }

    VkPipeline presentPipeline;
    {
        auto const vertex_binary = util::create_patched_spirv_from_binary_file("res/pr/liveness_sample/shader/spirv/vertex_blit.spv");
        auto const pixel_binary = util::create_patched_spirv_from_binary_file("res/pr/liveness_sample/shader/spirv/pixel_blit.spv");
        CC_DEFER
        {
            util::free_patched_spirv(vertex_binary);
            util::free_patched_spirv(pixel_binary);
        };

        cc::capped_vector<arg::shader_stage, 6> shader_stages;
        shader_stages.push_back(arg::shader_stage{vertex_binary.bytecode, vertex_binary.bytecode_size, shader_domain::vertex});
        shader_stages.push_back(arg::shader_stage{pixel_binary.bytecode, pixel_binary.bytecode_size, shader_domain::pixel});

        presentPipeline = create_fullscreen_pipeline(bv.mDevice.getDevice(), bv.mSwapchain.getRenderPass(), present_pipeline_layout.raw_layout, shader_stages);
    }

    buffer vert_buf;
    buffer ind_buf;
    unsigned num_indices;

    {
        auto const mesh_data = assets::load_obj_mesh(pr_test::sample_mesh_path);
        num_indices = unsigned(mesh_data.indices.size());

        vert_buf = create_vertex_buffer_from_data<assets::simple_vertex>(bv.mAllocator, uploadHeap, mesh_data.vertices);
        ind_buf = create_index_buffer_from_data<int>(bv.mAllocator, uploadHeap, mesh_data.indices);
    }

    image albedo_image;
    VkImageView albedo_view;
    {
        albedo_image = create_texture_from_file(bv.mAllocator, uploadHeap, pr_test::sample_texture_path);
        albedo_view = make_image_view(bv.mDevice.getDevice(), albedo_image, VK_FORMAT_R8G8B8A8_UNORM);
    }

    uploadHeap.flushAndFinish();

    descriptor_set_bundle arc_desc_set;
    {
        // we just need a single one of these, since we do not update it during dispatch
        // we just re-bind at different offsets

        arc_desc_set.initialize(descAllocator, arc_pipeline_layout);

        legacy::shader_argument shader_arg_zero;
        shader_arg_zero.cbv = dynamicBufferRing.getBuffer();
        shader_arg_zero.cbv_view_offset = 0;
        shader_arg_zero.cbv_view_size = sizeof(tg::mat4);

        arc_desc_set.update_argument(bv.mDevice.getDevice(), 0, shader_arg_zero);

        legacy::shader_argument shader_arg_one;
        shader_arg_one.srvs.push_back(albedo_view);
        shader_arg_one.cbv = dynamicBufferRing.getBuffer();
        shader_arg_one.cbv_view_offset = 0;
        shader_arg_one.cbv_view_size = sizeof(tg::mat4);

        arc_desc_set.update_argument(bv.mDevice.getDevice(), 1, shader_arg_one);
    }


    descriptor_set_bundle present_desc_set;
    {
        present_desc_set.initialize(descAllocator, present_pipeline_layout);

        legacy::shader_argument arg_zero;
        arg_zero.srvs.push_back(color_rt_view);
        present_desc_set.update_argument(bv.mDevice.getDevice(), 0, arg_zero);
    }

    auto const on_swapchain_resize_func = [&](tg::ivec2 backbuffer_size) {
        // recreate depth buffer
        bv.mAllocator.free(depth_image);
        depth_image = create_depth_stencil(bv.mAllocator, unsigned(backbuffer_size.x), unsigned(backbuffer_size.y), VK_FORMAT_D32_SFLOAT);
        vkDestroyImageView(bv.mDevice.getDevice(), depth_view, nullptr);
        depth_view = make_image_view(bv.mDevice.getDevice(), depth_image, VK_FORMAT_D32_SFLOAT);

        // recreate color rt buffer
        bv.mAllocator.free(color_rt_image);
        color_rt_image = create_render_target(bv.mAllocator, unsigned(backbuffer_size.x), unsigned(backbuffer_size.y), VK_FORMAT_R16G16B16A16_SFLOAT);
        vkDestroyImageView(bv.mDevice.getDevice(), color_rt_view, nullptr);
        color_rt_view = make_image_view(bv.mDevice.getDevice(), color_rt_image, VK_FORMAT_R16G16B16A16_SFLOAT);

        // update RT-dependent descriptor sets
        {
            legacy::shader_argument arg_zero;
            arg_zero.srvs.push_back(color_rt_view);
            present_desc_set.update_argument(bv.mDevice.getDevice(), 0, arg_zero);
        }

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
            fb_info.width = unsigned(backbuffer_size.x);
            fb_info.height = unsigned(backbuffer_size.y);
            fb_info.layers = 1;
            PR_VK_VERIFY_SUCCESS(vkCreateFramebuffer(bv.mDevice.getDevice(), &fb_info, nullptr, &arcFramebuffer));
        }

        // flush
        vkDeviceWaitIdle(bv.mDevice.getDevice());
        std::cout << "resized swapchain to: " << backbuffer_size.x << "x" << backbuffer_size.y << std::endl;
    };

    auto const on_window_resize_func = [&](int width, int height) {
        bv.mSwapchain.onResize(width, height);
        std::cout << "resized window to " << width << "x" << height << std::endl;
    };

    device::Timer timer;
    double run_time = 0.0;

    auto framecounter = 0;
    while (!window.isRequestingClose())
    {
        window.pollEvents();
        if (window.isPendingResize())
        {
            if (!window.isMinimized())
            {
                on_window_resize_func(window.getWidth(), window.getHeight());
            }
            window.clearPendingResize();
        }

        if (!window.isMinimized())
        {
            if (bv.mSwapchain.hasBackbufferResized())
            {
                // The swapchain has resized, recreate depending resources
                on_swapchain_resize_func(bv.mSwapchain.getBackbufferSize());
                bv.mSwapchain.clearBackbufferResizeFlag();
            }

            auto const frametime = timer.elapsedMillisecondsD();
            timer.restart();
            run_time += frametime / 1000.;

            {
                ++framecounter;
                if (framecounter == 60)
                {
                    std::cout << "Frametime: " << frametime << "ms" << std::endl;
                    framecounter = 0;
                }
            }

            if (!bv.mSwapchain.waitForBackbuffer())
            {
                // The swapchain was out of date and has resized, discard this frame
                continue;
            }

            commandBufferRing.onBeginFrame();
            dynamicBufferRing.onBeginFrame();

            auto const backbuffer_size = bv.mSwapchain.getBackbufferSize();

            handle::command_list const cmdlist_handle = bv.recordCommandList(nullptr, 0);
            auto const cmd_buf = bv.mPoolCmdLists.getRawBuffer(cmdlist_handle);

            {
                // transition color_rt to render target
                barrier_bundle<1> barrier;
                barrier.add_image_barrier(color_rt_image.image, state_change{resource_state::shader_resource, shader_domain::pixel, resource_state::render_target});
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

                util::set_viewport(cmd_buf, backbuffer_size.x, backbuffer_size.y);
                vkCmdBeginRenderPass(cmd_buf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, arcPipeline);

                VkBuffer vertex_buffers[] = {vert_buf.buffer};
                VkDeviceSize offsets[] = {0};

                vkCmdBindVertexBuffers(cmd_buf, 0, 1, vertex_buffers, offsets);
                vkCmdBindIndexBuffer(cmd_buf, ind_buf.buffer, 0, VK_INDEX_TYPE_UINT32);


                {
                    VkDescriptorBufferInfo cb_upload_info;
                    {
                        struct mvp_data
                        {
                            tg::mat4 vp;
                        };

                        {
                            mvp_data* data;
                            dynamicBufferRing.allocConstantBufferTyped(data, cb_upload_info);
                            data->vp = pr_test::get_view_projection_matrix(run_time, window.getWidth(), window.getHeight());
                        }
                    }

                    auto const dynamic_offset = uint32_t(cb_upload_info.offset);

                    arc_desc_set.bind_argument(cmd_buf, arc_pipeline_layout.raw_layout, 0, dynamic_offset);
                }

                {
                    pr_test::model_matrix_data* vert_cb_data;
                    VkDescriptorBufferInfo cb_upload_info;
                    dynamicBufferRing.allocConstantBufferTyped(vert_cb_data, cb_upload_info);

                    // record model matrices
                    vert_cb_data->fill(run_time);

                    for (auto i = 0u; i < vert_cb_data->model_matrices.size(); ++i)
                    {
                        auto const dynamic_offset = uint32_t(i * sizeof(vert_cb_data->model_matrices[0]));
                        auto const combined_offset = uint32_t(cb_upload_info.offset) + dynamic_offset;

                        arc_desc_set.bind_argument(cmd_buf, arc_pipeline_layout.raw_layout, 1, combined_offset);
                        vkCmdDrawIndexed(cmd_buf, num_indices, 1, 0, 0, 0);
                    }
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

                util::set_viewport(cmd_buf, backbuffer_size.x, backbuffer_size.y);
                vkCmdBeginRenderPass(cmd_buf, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipeline);

                present_desc_set.bind_argument(cmd_buf, present_pipeline_layout.raw_layout, 0);

                vkCmdDraw(cmd_buf, 3, 1, 0, 0);
                vkCmdEndRenderPass(cmd_buf);
            }

            PR_VK_VERIFY_SUCCESS(vkEndCommandBuffer(cmd_buf));


            bv.submit(cc::span{cmdlist_handle});

            bv.mSwapchain.performPresentSubmit();
            bv.mSwapchain.present();
        }
    }

    {
        auto const& device = bv.mDevice.getDevice();

        vkDeviceWaitIdle(device);

        bv.mAllocator.free(vert_buf);
        bv.mAllocator.free(ind_buf);

        bv.mAllocator.free(albedo_image);
        vkDestroyImageView(device, albedo_view, nullptr);

        bv.mAllocator.free(depth_image);
        vkDestroyImageView(device, depth_view, nullptr);

        bv.mAllocator.free(color_rt_image);
        vkDestroyImageView(device, color_rt_view, nullptr);

        vkDestroyFramebuffer(device, arcFramebuffer, nullptr);
        vkDestroyPipeline(device, arcPipeline, nullptr);
        vkDestroyRenderPass(device, arcRenderPass, nullptr);

        vkDestroyPipeline(device, presentPipeline, nullptr);

        arc_pipeline_layout.free(device);
        present_pipeline_layout.free(device);

        arc_desc_set.free(descAllocator);
        present_desc_set.free(descAllocator);

        descAllocator.destroy();
    }
}

#endif
