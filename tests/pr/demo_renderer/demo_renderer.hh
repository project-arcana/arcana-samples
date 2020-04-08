#pragma once

#include <arcana-incubator/device-abstraction/device_abstraction.hh>

#include <phantasm-renderer/Context.hh>

#include <arcana-incubator/pr-util/resource_loading.hh>
#include <arcana-incubator/pr-util/texture_processing.hh>

#include "passes.hh"

namespace dr
{
class DemoRenderer
{
public:
    DemoRenderer(inc::da::SDLWindow& window);
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
        while (!mWindow.isRequestingClose())
        {
            if (!handleResizes())
                continue;

            func(mContext);
        }

        mContext.flush();
    }

    void execute();

private:
    bool handleResizes();

    void onBackbufferResize(tg::isize2 new_size);

private:
    inc::da::SDLWindow& mWindow;
    pr::Context mContext;

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
