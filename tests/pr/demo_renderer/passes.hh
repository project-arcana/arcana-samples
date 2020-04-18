#pragma once

#include <phantasm-renderer/resource_types.hh>

#include "scene.hh"
#include "targets.hh"

namespace dmr
{
struct pass
{
    virtual void init(pr::Context& ctx) = 0;
    virtual void execute(pr::Context& ctx, pr::raii::Frame& frame, global_targets& targets, scene& scene) = 0;
    virtual ~pass() = default;
};

struct depthpre_pass : public pass
{
    void init(pr::Context& ctx) override;
    void execute(pr::Context& ctx, pr::raii::Frame& frame, global_targets& targets, scene& scene) override;

    pr::auto_graphics_pipeline_state pso_depthpre;
};

struct forward_pass : public pass
{
    void init(pr::Context& ctx) override;
    void execute(pr::Context& ctx, pr::raii::Frame& frame, global_targets& targets, scene& scene) override;

    pr::auto_graphics_pipeline_state pso_pbr;

    pr::auto_prebuilt_argument sv_ibl;
    pr::auto_texture tex_ibl_spec;
    pr::auto_texture tex_ibl_irr;
    pr::auto_texture tex_ibl_lut;
};

struct taa_pass : public pass
{
    void init(pr::Context& ctx) override;
    void execute(pr::Context& ctx, pr::raii::Frame& frame, global_targets& targets, scene& scene) override;

    pr::auto_graphics_pipeline_state pso_taa;
};

struct postprocess_pass : public pass
{
    void init(pr::Context& ctx) override;
    void execute(pr::Context& ctx, pr::raii::Frame& frame, global_targets& targets, scene& scene) override;

    void clear_target(pr::raii::Frame& frame, pr::render_target const& target);

    pr::auto_graphics_pipeline_state pso_downsample;
    pr::auto_graphics_pipeline_state pso_bloom;
    pr::auto_graphics_pipeline_state pso_tonemap;
    pr::auto_graphics_pipeline_state pso_output;
    pr::auto_graphics_pipeline_state pso_clear;
};
}
