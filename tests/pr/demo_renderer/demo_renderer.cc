#include "demo_renderer.hh"

#include <fstream>

#include <phantasm-hardware-interface/config.hh>

#include <phantasm-renderer/CompiledFrame.hh>
#include <phantasm-renderer/Frame.hh>

#include <arcana-incubator/device-abstraction/stringhash.hh>
#include <arcana-incubator/imgui/imgui_impl_sdl2.hh>

namespace
{
enum e_input : uint64_t
{
    ge_input_logpos = 50,
    ge_input_setpos
};

bool verify_workdir()
{
    std::ifstream file("res/res_canary");
    return file.good();
}

bool verify_shaders_compiled() { return inc::pre::is_shader_present("misc/imgui_vs", "res/pr/demo_render/bin/"); }
}

void dmr::DemoRenderer::initialize(inc::da::SDLWindow& window, pr::backend backend_type)
{
    CC_ASSERT(mWindow == nullptr && "double initialize");

    // onboarding asserts
    CC_ASSERT(verify_workdir() && "cannot read res/ folder, set executable working directory to <path>/arcana-samples/ (root)");
    CC_ASSERT(verify_shaders_compiled() && "cant find shaders, run res/pr/demo_render/compile_shaders.bat/.sh");

    mWindow = &window;

    // input setup
    mInput.initialize(100);
    mCamera.setup_default_inputs(mInput);

    mInput.bindKey(ge_input_logpos, SDL_SCANCODE_H);
    mInput.bindKey(ge_input_setpos, SDL_SCANCODE_J);

    phi::backend_config config;
    config.adapter = phi::adapter_preference::highest_vram;
    config.validation = phi::validation_level::on_extended;
    config.present = phi::present_mode::allow_tearing;

    mContext.initialize({mWindow->getSdlWindow()}, backend_type, config);

    {
        auto [vs, vs_b] = inc::pre::load_shader(mContext, "misc/imgui_vs", phi::shader_stage::vertex, "res/pr/demo_render/bin/");
        auto [ps, ps_b] = inc::pre::load_shader(mContext, "misc/imgui_ps", phi::shader_stage::pixel, "res/pr/demo_render/bin/");

        ImGui::SetCurrentContext(ImGui::CreateContext(nullptr));
        ImGui_ImplSDL2_Init(mWindow->getSdlWindow());
        mImguiImpl.initialize(&mContext.get_backend(), ps_b.get(), ps_b.size(), vs_b.get(), vs_b.size());
    }

    mTexProcessingPSOs.init(mContext, "res/pr/demo_render/bin/preprocess/");

    // load and preprocess IBL resources
    inc::pre::filtered_specular_result specular_intermediate;
    {
        auto frame = mContext.make_frame();

        specular_intermediate = mTexProcessingPSOs.load_filtered_specular_map(frame, "res/arcana-sample-resources/phi/texture/ibl/mono_lake.hdr");

        mPasses.forward.tex_ibl_spec = cc::move(specular_intermediate.filtered_env);
        mPasses.forward.tex_ibl_irr = mTexProcessingPSOs.create_diffuse_irradiance_map(frame, mPasses.forward.tex_ibl_spec);
        mPasses.forward.tex_ibl_lut = mTexProcessingPSOs.create_brdf_lut(frame, 256);

        frame.transition(mPasses.forward.tex_ibl_spec, phi::resource_state::shader_resource, phi::shader_stage::pixel);
        frame.transition(mPasses.forward.tex_ibl_irr, phi::resource_state::shader_resource, phi::shader_stage::pixel);
        frame.transition(mPasses.forward.tex_ibl_lut, phi::resource_state::shader_resource, phi::shader_stage::pixel);

        mContext.submit(cc::move(frame));
    }

    mPasses.depthpre.init(mContext);
    mPasses.forward.init(mContext);
    mPasses.taa.init(mContext);
    mPasses.postprocess.init(mContext);

    onBackbufferResize(mContext.get_backbuffer_size());

    mScene.init(mContext, 500);
    mContext.flush();
}

void dmr::DemoRenderer::destroy()
{
    if (mWindow != nullptr)
    {
        mContext.flush();

        mImguiImpl.destroy();
        ImGui_ImplSDL2_Shutdown();

        mPasses = {};
        mScene = {};
        mTargets = {};
        mUniqueSVs.clear();
        mUniqueTextures.clear();
        mUniqueMeshes.clear();
        mTexProcessingPSOs.free();

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

dmr::material dmr::DemoRenderer::loadMaterial(const char* p_albedo, const char* p_normal, const char* p_metal, const char* p_rough)
{
    auto frame = mContext.make_frame();

    auto albedo = mTexProcessingPSOs.load_texture(frame, p_albedo, pr::format::rgba8un, true, true);
    auto normal = mTexProcessingPSOs.load_texture(frame, p_normal, pr::format::rgba8un, true, false);
    auto metal = mTexProcessingPSOs.load_texture(frame, p_metal, pr::format::r8un, true, false);
    auto rough = mTexProcessingPSOs.load_texture(frame, p_rough, pr::format::r8un, true, false);

    frame.transition(albedo, phi::resource_state::shader_resource, phi::shader_stage::pixel);
    frame.transition(normal, phi::resource_state::shader_resource, phi::shader_stage::pixel);
    frame.transition(metal, phi::resource_state::shader_resource, phi::shader_stage::pixel);
    frame.transition(rough, phi::resource_state::shader_resource, phi::shader_stage::pixel);

    mContext.submit(cc::move(frame));

    auto const& new_sv = mUniqueSVs.push_back(mContext.build_argument().add(albedo).add(normal).add(metal).add(rough).make_graphics());
    dmr::material res;
    res.outer_sv = new_sv;

    // move to unique array to keep alive
    mUniqueTextures.push_back(cc::move(albedo));
    mUniqueTextures.push_back(cc::move(normal));
    mUniqueTextures.push_back(cc::move(metal));
    mUniqueTextures.push_back(cc::move(rough));

    return res;
}

void dmr::DemoRenderer::execute(float dt)
{
    ImGui::Begin("pr dmr", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    ImGui::SetWindowSize(ImVec2{175, 150}, ImGuiCond_Always);
    ImGui::Text("frametime: %.2f ms", double(dt * 1000.f));
    ImGui::Text("WASD - move\nE/Q - raise/lower\nhold RMB - mouselook\nshift - speedup\nctrl - slowdown");
    ImGui::End();

    mScene.on_next_frame();

    // camera update
    {
        mCamera.update_default_inputs(mWindow->getSdlWindow(), mInput, dt);

        if (mInput.get(ge_input_logpos).wasPressed())
        {
            LOG(info) << mCamera.physical.forward << mCamera.physical.position;
        }

        if (mInput.get(ge_input_setpos).wasPressed())
        {
            mCamera.target.forward = {-0.243376f, 0.431579f, 0.868624f};
            mCamera.target.position = {-0.551521f, 1.68109f, 2.92037f};
        }

        mScene.camdata.fill_data(mScene.resolution, mCamera.physical.position, mCamera.physical.forward, mScene.halton_index);
    }
    mScene.upload_current_frame();

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

        auto backbuffer = mContext.acquire_backbuffer();
        mPasses.postprocess.execute_output(mContext, frame, mTargets, mScene, backbuffer);

        ImGui::Render();
        auto* const imgui_drawdata = ImGui::GetDrawData();
        auto const imgui_framesize = mImguiImpl.get_command_size(imgui_drawdata);
        mImguiImpl.write_commands(imgui_drawdata, backbuffer.res.handle, frame.write_raw_bytes(imgui_framesize), imgui_framesize);

        frame.present_after_submit(backbuffer);
        cf_post = mContext.compile(cc::move(frame));
    }

    mScene.flush_current_frame_upload(mContext);
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
        mContext.on_window_resize(mWindow->getSize());

    if (mContext.clear_backbuffer_resize())
        onBackbufferResize(mContext.get_backbuffer_size());

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
