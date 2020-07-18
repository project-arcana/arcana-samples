#include "demo_renderer.hh"

#include <fstream>

#include <phantasm-hardware-interface/config.hh>

#include <phantasm-renderer/CompiledFrame.hh>
#include <phantasm-renderer/Frame.hh>

#include <dxc-wrapper/file_util.hh>

#include <arcana-incubator/device-abstraction/stringhash.hh>
#include <arcana-incubator/imgui/imgui_impl_phi.hh>
#include <arcana-incubator/imgui/imgui_impl_sdl2.hh>

namespace
{
enum e_input : uint64_t
{
    ge_input_reset_cam = 50,
};

bool verify_workdir()
{
    std::ifstream file("res/res_canary");
    return file.good();
}

bool verify_shaders_compiled() { return inc::pre::is_shader_present("misc/imgui_vs", "res/pr/demo_render/bin/"); }

// help or attempt to recover likely errors during first launch (wrong workdir or shaders not compiled)
bool run_onboarding_test()
{
    if (!verify_workdir())
    {
        LOG_ERROR("cant read res/ folder, set executable working directory to <path>/arcana-samples/ (root)");
        return false;
    }
    else if (!verify_shaders_compiled())
    {
        LOG_WARN("shaders not compiled, run res/pr/demo_render/compiler_shaders.bat/.sh");
        LOG_WARN("attempting live compilation");
        dxcw::compiler comp;
        comp.initialize();
        dxcw::shaderlist_compilation_result res;
        dxcw::compile_shaderlist(comp, "res/pr/demo_render/src/shaderlist.txt", &res);
        comp.destroy();

        if (res.num_shaders_detected == -1 || !verify_shaders_compiled())
        {
            LOG_ERROR("failed to compile shaders live");
            return false;
        }
        else
        {
            LOG_INFO("live compilation succeeded");
        }
    }

    return true;
}
}

void dmr::DemoRenderer::initialize(inc::da::SDLWindow& window, pr::backend backend_type)
{
    CC_ASSERT(mWindow == nullptr && "double initialize");
    CC_ASSERT(run_onboarding_test() && "critical error, onboarding cannot recover");

    mWindow = &window;

    // input setup
    mInput.initialize(100);
    mCamera.setup_default_inputs(mInput);

    mInput.bindKey(ge_input_reset_cam, inc::da::scancode::sc_H);

    phi::backend_config config;
    config.adapter = phi::adapter_preference::highest_vram;
    config.validation = phi::validation_level::on_extended;

    mContext.initialize(backend_type, config);
    mSwapchain = mContext.make_swapchain({mWindow->getSdlWindow()}, mWindow->getSize());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    if (backend_type == pr::backend::d3d12)
        ImGui_ImplSDL2_InitForD3D(window.getSdlWindow());
    else
        ImGui_ImplSDL2_InitForVulkan(window.getSdlWindow());
    window.setEventCallback(ImGui_ImplSDL2_ProcessEvent);

    ImGui_ImplPHI_Init(&mContext.get_backend(), 3, phi::format::bgra8un);


    mTexProcessingPSOs.init(mContext, "res/pr/demo_render/bin/preprocess/");

    // load and preprocess IBL resources
    inc::pre::filtered_specular_result specular_intermediate;
    {
        auto frame = mContext.make_frame();

        specular_intermediate
            = mTexProcessingPSOs.load_filtered_specular_map_from_file(frame, "res/arcana-sample-resources/phi/texture/ibl/mono_lake.hdr");

        mPasses.forward.tex_ibl_spec = cc::move(specular_intermediate.filtered_env);
        mPasses.forward.tex_ibl_irr = mTexProcessingPSOs.create_diffuse_irradiance_map(frame, mPasses.forward.tex_ibl_spec);
        mPasses.forward.tex_ibl_lut = mTexProcessingPSOs.create_brdf_lut(frame, 256);

        frame.transition(mPasses.forward.tex_ibl_spec, pr::state::shader_resource, pr::shader::pixel);
        frame.transition(mPasses.forward.tex_ibl_irr, pr::state::shader_resource, pr::shader::pixel);
        frame.transition(mPasses.forward.tex_ibl_lut, pr::state::shader_resource, pr::shader::pixel);

        mContext.submit(cc::move(frame));
    }

    mPasses.depthpre.init(mContext);
    mPasses.forward.init(mContext);
    mPasses.taa.init(mContext);
    mPasses.postprocess.init(mContext);

    onBackbufferResize(mContext.get_backbuffer_size(mSwapchain));

    mScene.init(mContext, mContext.get_num_backbuffers(mSwapchain), 500);
    mContext.flush();
}

void dmr::DemoRenderer::destroy()
{
    if (mWindow != nullptr)
    {
        mContext.flush();

        ImGui_ImplPHI_Shutdown();
        ImGui_ImplSDL2_Shutdown();

        mPasses = {};
        mScene = {};
        mTargets = {};
        mUniqueSVs.clear();
        mUniqueTextures.clear();
        mUniqueMeshes.clear();
        mTexProcessingPSOs.free();

        mSwapchain.free();

        mContext.destroy();
        mWindow = nullptr;
    }
}

dmr::mesh dmr::DemoRenderer::loadMesh(const char* path, bool binary)
{
    auto const& new_mesh = mUniqueMeshes.push_back(inc::pre::load_mesh(mContext, path, binary));
    dmr::mesh res;
    res.vertex = new_mesh.vertex;
    res.index = new_mesh.index;
    return res;
}

dmr::material dmr::DemoRenderer::loadMaterial(const char* p_albedo, const char* p_normal, const char* p_arm)
{
    auto frame = mContext.make_frame();

    auto albedo = mTexProcessingPSOs.load_texture_from_file(frame, p_albedo, pr::format::rgba8un, true, true);
    auto normal = mTexProcessingPSOs.load_texture_from_file(frame, p_normal, pr::format::rgba8un, true, false);
    auto ao_rough_metal = mTexProcessingPSOs.load_texture_from_file(frame, p_arm, pr::format::rgba8un, true, false);

    frame.transition(albedo, pr::state::shader_resource, pr::shader::pixel);
    frame.transition(normal, pr::state::shader_resource, pr::shader::pixel);
    frame.transition(ao_rough_metal, pr::state::shader_resource, pr::shader::pixel);

    mContext.submit(cc::move(frame));

    auto const& new_sv = mUniqueSVs.push_back(mContext.build_argument().add(albedo).add(normal).add(ao_rough_metal).make_graphics());
    dmr::material res;
    res.outer_sv = new_sv;

    // move to unique array to keep alive
    mUniqueTextures.push_back(cc::move(albedo));
    mUniqueTextures.push_back(cc::move(normal));
    mUniqueTextures.push_back(cc::move(ao_rough_metal));

    return res;
}

void dmr::DemoRenderer::execute(float dt)
{
    ImGui::Begin("pr dmr", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    ImGui::SetWindowSize(ImVec2{210, 220}, ImGuiCond_Always);
    ImGui::Text("frametime: %.2f ms", double(dt * 1000.f));
    ImGui::Text("cam pos: %.2f %.2f %.2f", double(mCamera.physical.position.x), double(mCamera.physical.position.y), double(mCamera.physical.position.z));
    ImGui::Text("cam fwd: %.2f %.2f %.2f", double(mCamera.physical.forward.x), double(mCamera.physical.forward.y), double(mCamera.physical.forward.z));
    ImGui::Separator();
    ImGui::Text("WASD - move\nE/Q - raise/lower\nhold RMB - mouselook\nshift - speedup\nctrl - slowdown");
    ImGui::End();

    mScene.on_next_frame();

    // camera update
    {
        if (mInput.get(ge_input_reset_cam).wasPressed())
        {
            mCamera.target = {};
        }


        mCamera.update_default_inputs(mWindow->getSdlWindow(), mInput, dt);
        mScene.camdata.fill_data(mScene.resolution, mCamera.physical.position, mCamera.physical.forward, mScene.halton_index);
    }
    mScene.upload_current_frame(mContext);

    pr::CompiledFrame cf_depthpre;
    pr::CompiledFrame cf_forward;
    pr::CompiledFrame cf_post;

    {
        auto frame = mContext.make_frame();
        mPasses.depthpre.execute(mContext, frame, mTargets, mScene);
        cf_depthpre = mContext.compile(cc::move(frame));
    }

    {
        auto frame = mContext.make_frame();
        mPasses.forward.execute(mContext, frame, mTargets, mScene);
        cf_forward = mContext.compile(cc::move(frame));
    }

    {
        auto frame = mContext.make_frame();
        mPasses.taa.execute(mContext, frame, mTargets, mScene);

        auto backbuffer = mContext.acquire_backbuffer(mSwapchain);
        mPasses.postprocess.execute_output(mContext, frame, mTargets, mScene, backbuffer);

        ImGui::Render();
        auto* const imgui_drawdata = ImGui::GetDrawData();
        auto const imgui_framesize = ImGui_ImplPHI_GetDrawDataCommandSize(imgui_drawdata);
        {
            auto fb = frame.build_framebuffer().loaded_target(backbuffer).make();
            frame.begin_debug_label("imgui");
            ImGui_ImplPHI_RenderDrawData(imgui_drawdata, {frame.write_raw_bytes(imgui_framesize), imgui_framesize});

            frame.end_debug_label();
        }
        frame.present_after_submit(backbuffer, mSwapchain);
        cf_post = mContext.compile(cc::move(frame));
    }

    mContext.submit(cc::move(cf_depthpre));
    mContext.submit(cc::move(cf_forward));
    mContext.submit(cc::move(cf_post));
}

bool dmr::DemoRenderer::handleEvents()
{
    // input and polling
    {
        mInput.updatePrePoll();

        SDL_Event e;
        while (mWindow->pollSingleEvent(e))
            mInput.processEvent(e);

        mInput.updatePostPoll();
    }

    if (mWindow->isMinimized())
        return false;

    if (mWindow->clearPendingResize())
        mContext.on_window_resize(mSwapchain, mWindow->getSize());

    if (mContext.clear_backbuffer_resize(mSwapchain))
        onBackbufferResize(mContext.get_backbuffer_size(mSwapchain));

    ImGui_ImplPHI_NewFrame();
    ImGui_ImplSDL2_NewFrame(mWindow->getSdlWindow());
    ImGui::NewFrame();

    return true;
}

void dmr::DemoRenderer::onBackbufferResize(tg::isize2 new_size)
{
    mTargets.recreate_rts(mContext, new_size);
    mTargets.recreate_buffers(mContext, new_size);
    mScene.resolution = new_size;

    // clear history targets explicitly
    auto frame = mContext.make_frame();
    mPasses.postprocess.clear_target(frame, mTargets.t_history_a);
    mPasses.postprocess.clear_target(frame, mTargets.t_history_b);
    mContext.submit(cc::move(frame));
}
