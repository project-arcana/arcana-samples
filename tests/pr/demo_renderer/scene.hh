#pragma once

#include <clean-core/capped_array.hh>
#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg-lean.hh>

#include <phantasm-renderer/argument.hh>
#include <phantasm-renderer/resource_types.hh>

namespace dr
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
    mesh mesh;
    material mat;
};

struct instance_gpudata
{
    tg::mat4 model;
    tg::mat4 mv; // model * view
};

struct scene_gpudata
{
    tg::mat4 proj;
    tg::mat4 proj_inv;
    tg::mat4 view;
    tg::mat4 view_inv;
    tg::mat4 vp;
    tg::mat4 vp_inv;
    tg::mat4 clean_proj;
    tg::mat4 prev_clean_proj;
    tg::pos3 cam_pos;
    float runtime;

    void fill_data(tg::isize2 res, tg::pos3 campos, tg::pos3 camtarget);
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
    scene_gpudata camdata;

    //
    // multi-buffered resources
    struct per_frame_resource_t
    {
        pr::auto_buffer cb_camdata;
        pr::auto_buffer sb_modeldata;

        std::byte* cb_camdata_map = nullptr;
        std::byte* sb_modeldata_map = nullptr;
    };

    cc::capped_array<per_frame_resource_t, 5> per_frame_resources;
    unsigned num_backbuffers = 0;
    unsigned current_frame_index = 0;

    void init(pr::Context& ctx, unsigned max_num_instances);

    per_frame_resource_t& last_frame() { return per_frame_resources[cc::wrapped_decrement(current_frame_index, num_backbuffers)]; }
    per_frame_resource_t& current_frame() { return per_frame_resources[current_frame_index]; }
    per_frame_resource_t& next_frame() { return per_frame_resources[cc::wrapped_increment(current_frame_index, num_backbuffers)]; }

    void on_next_frame();

    void upload_current_frame();
    void flush_current_frame_upload(pr::Context& ctx);
};
}
