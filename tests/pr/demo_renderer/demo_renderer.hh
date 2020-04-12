#pragma once

#include <arcana-incubator/device-abstraction/device_abstraction.hh>

#include <phantasm-renderer/Context.hh>

#include <arcana-incubator/device-abstraction/freefly_camera.hh>
#include <arcana-incubator/device-abstraction/input.hh>
#include <arcana-incubator/device-abstraction/timer.hh>
#include <arcana-incubator/pr-util/resource_loading.hh>
#include <arcana-incubator/pr-util/texture_processing.hh>

#include "passes.hh"

namespace dr
{
class DemoRenderer
{
public:
    DemoRenderer(inc::da::SDLWindow& window, pr::backend_type backend_type);
    ~DemoRenderer();

    /// loads a mesh from disk
    [[nodiscard]] dr::mesh loadMesh(char const* path, bool binary = false);

    [[nodiscard]] dr::material loadMaterial(char const* p_albedo, char const* p_normal, char const* p_metal, char const* p_rough);

    void addInstance(dr::mesh const& mesh, dr::material const& mat, tg::mat4 transform)
    {
        mScene.instances.push_back({mesh, mat});
        mScene.instance_transforms.push_back({transform});
    }

    template <class F>
    void mainLoop(F&& func)
    {
        mTimer.restart();
        while (!mWindow.isRequestingClose())
        {
            if (!handleEvents())
                continue;

            auto const dt = mTimer.elapsedSeconds();
            mTimer.restart();
            func(mContext, dt);
        }

        mContext.flush();
    }

    void execute(float dt);

private:
    bool handleEvents();

    void onBackbufferResize(tg::isize2 new_size);

private:
    inc::da::SDLWindow& mWindow;
    pr::Context mContext;

    inc::da::Timer mTimer;
    inc::da::input_manager mInput;
    inc::da::smooth_fps_cam mCamera;
    bool mMouseCaptured = false;

    inc::pre::texture_processing mTexProcessingPSOs;

    cc::vector<inc::pre::pr_mesh> mUniqueMeshes;
    cc::vector<pr::auto_texture> mUniqueTextures;
    cc::vector<pr::auto_prebuilt_argument> mUniqueSVs;

    global_targets mTargets;
    scene mScene;

    struct
    {
        depthpre_pass depthpre;
        forward_pass forward;
        taa_pass taa;
        postprocess_pass postprocess;
    } mPasses;
};

}