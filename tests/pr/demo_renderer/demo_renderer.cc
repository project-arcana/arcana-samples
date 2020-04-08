#include "demo_renderer.hh"

#include <phantasm-hardware-interface/config.hh>

#include <phantasm-renderer/CompiledFrame.hh>
#include <phantasm-renderer/Frame.hh>

dr::DemoRenderer::DemoRenderer(inc::da::SDLWindow& window, pr::backend_type backend_type) : mWindow(window)
{
    mWindow.initialize("Demo Renderer");

    phi::backend_config config;
    config.adapter = phi::adapter_preference::highest_vram;
    config.validation = phi::validation_level::on_extended;

    mContext.initialize({mWindow.getSdlWindow()}, backend_type, config);

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

        mContext.submit(frame);
    }

    mPasses.depthpre.init(mContext);
    mPasses.forward.init(mContext);
    mPasses.taa.init(mContext);
    mPasses.postprocess.init(mContext);

    onBackbufferResize(mContext.get_backbuffer_size());

    mScene.init(mContext, 500);
    mContext.flush();
}

dr::DemoRenderer::~DemoRenderer() { mContext.flush(); }

dr::mesh dr::DemoRenderer::loadMesh(const char* path, bool binary)
{
    auto const& new_mesh = mUniqueMeshes.push_back(inc::pre::load_mesh(mContext, path, binary));
    dr::mesh res;
    res.vertex = new_mesh.vertex;
    res.index = new_mesh.index;
    return res;
}

dr::material dr::DemoRenderer::loadMaterial(const char* p_albedo, const char* p_normal, const char* p_metal, const char* p_rough)
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

    mContext.submit(frame);

    auto const& new_sv = mUniqueSVs.push_back(mContext.build_argument().add(albedo).add(normal).add(metal).add(rough).make_graphics());
    dr::material res;
    res.outer_sv = new_sv;

    // move to unique array to keep alive
    mUniqueTextures.push_back(cc::move(albedo));
    mUniqueTextures.push_back(cc::move(normal));
    mUniqueTextures.push_back(cc::move(metal));
    mUniqueTextures.push_back(cc::move(rough));

    return res;
}

void dr::DemoRenderer::execute()
{
    mScene.on_next_frame();

    mScene.camdata.fill_data(mScene.resolution, tg::pos3(5, 5, 5), tg::pos3(0, 0, 0));
    mScene.upload_current_frame();

    pr::CompiledFrame cf_depthpre;
    pr::CompiledFrame cf_forward;
    pr::CompiledFrame cf_post;

    {
        auto frame = mContext.make_frame();
        mPasses.depthpre.execute(mContext, frame, mTargets, mScene);
        cf_depthpre = mContext.compile(frame);
    }

    {
        auto frame = mContext.make_frame();
        mPasses.forward.execute(mContext, frame, mTargets, mScene);
        cf_forward = mContext.compile(frame);
    }

    {
        auto frame = mContext.make_frame();
        mPasses.taa.execute(mContext, frame, mTargets, mScene);
        mPasses.postprocess.execute(mContext, frame, mTargets, mScene);
        cf_post = mContext.compile(frame);
    }

    mScene.flush_current_frame_upload(mContext);
    mContext.submit(cc::move(cf_depthpre));
    mContext.submit(cc::move(cf_forward));
    mContext.submit(cc::move(cf_post));
    mContext.present();
}

bool dr::DemoRenderer::handleResizes()
{
    mWindow.pollEvents();
    if (mWindow.isMinimized())
        return false;

    if (mWindow.clearPendingResize())
        mContext.on_window_resize(mWindow.getSize());

    if (mContext.clear_backbuffer_resize())
        onBackbufferResize(mContext.get_backbuffer_size());

    return true;
}

void dr::DemoRenderer::onBackbufferResize(tg::isize2 new_size)
{
    mTargets.recreate_rts(mContext, new_size);
    mTargets.recreate_buffers(mContext, new_size);
    mScene.resolution = new_size;
}
