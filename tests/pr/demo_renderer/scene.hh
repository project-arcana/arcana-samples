#pragma once

#include <clean-core/capped_array.hh>
#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg-lean.hh>

#include <phantasm-renderer/argument.hh>
#include <phantasm-renderer/resource_types.hh>

namespace dmr
{
struct mesh
{
    pr::buffer vertex;
    pr::buffer index;
};

struct material
{
    //    pr::graphics_pipeline_state pso; // right now every instance has the same PSO in forward_pass
    pr::prebuilt_argument outer_sv;
};

struct instance
{
    struct mesh mesh;
    material mat;
};

struct instance_gpudata
{
    tg::mat4 model;
    tg::mat4 prev_model;

    instance_gpudata(tg::mat4 initial_transform) : model(initial_transform), prev_model(initial_transform) {}
};

struct scene_gpudata
{
    tg::mat4 proj;
    tg::mat4 proj_inv;
    tg::mat4 view;
    tg::mat4 view_inv;
    tg::mat4 vp;
    tg::mat4 vp_inv;
    tg::mat4 clean_vp;
    tg::mat4 clean_vp_inv;
    tg::mat4 prev_clean_vp;
    tg::mat4 prev_clean_vp_inv;

    void fill_data(tg::isize2 res, tg::pos3 campos, tg::vec3 camforward, unsigned halton_index);
};

struct scene
{
    //
    // parallel instance arrays
    cc::vector<instance> instances;
    cc::vector<instance_gpudata> instance_transforms;

    //
    // global data
    tg::isize2 resolution;
    unsigned halton_index = 0;
    bool is_history_a = true;
    scene_gpudata camdata;

    //
    // multi-buffered resources
    struct per_frame_resource_t
    {
        pr::auto_buffer cb_camdata;
        pr::auto_buffer sb_modeldata;
    };

    cc::capped_array<per_frame_resource_t, 5> per_frame_resources;
    unsigned num_backbuffers = 0;
    unsigned current_frame_index = 0;

    void init(pr::Context& ctx, unsigned max_num_instances);

    per_frame_resource_t& last_frame() { return per_frame_resources[cc::wrapped_decrement(current_frame_index, num_backbuffers)]; }
    per_frame_resource_t& current_frame() { return per_frame_resources[current_frame_index]; }
    per_frame_resource_t& next_frame() { return per_frame_resources[cc::wrapped_increment(current_frame_index, num_backbuffers)]; }

    void on_next_frame();

    void upload_current_frame(pr::Context& ctx);
};
}
