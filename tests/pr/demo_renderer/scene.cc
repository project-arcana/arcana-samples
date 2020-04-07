#include "scene.hh"

#include <phantasm-renderer/Context.hh>

#include <typed-geometry/tg.hh>

void dr::scene::init(pr::Context& ctx, unsigned max_num_instances)
{
    num_backbuffers = ctx.get_num_backbuffers();

    instances.reserve(max_num_instances);
    instance_transforms.reserve(max_num_instances);

    per_frame_resources.emplace(num_backbuffers);
    for (auto& per_frame : per_frame_resources)
    {
        per_frame.cb_camdata = ctx.make_upload_buffer(sizeof(scene_gpudata));
        per_frame.sb_modeldata = ctx.make_upload_buffer(sizeof(instance_gpudata) * max_num_instances, sizeof(instance_gpudata));

        per_frame.cb_camdata_map = ctx.get_buffer_map(per_frame.cb_camdata);
        per_frame.sb_modeldata_map = ctx.get_buffer_map(per_frame.sb_modeldata);
    }

    current_frame_index = 0;
}

void dr::scene::upload_current_frame()
{
    auto& frame = current_frame();

    std::memcpy(frame.cb_camdata_map, &camdata, sizeof(camdata));
    std::memcpy(frame.sb_modeldata_map, instance_transforms.data(), sizeof(instance_gpudata) * instance_transforms.size());
}

void dr::scene::flush_current_frame_upload(pr::Context& ctx)
{
    auto& frame = current_frame();
    ctx.flush_buffer_writes(frame.cb_camdata);
    ctx.flush_buffer_writes(frame.sb_modeldata);
}


void dr::camera::recalculate_proj(int w, int h)
{
    //
    projection = tg::perspective_directx(60_deg, w / float(h), 0.1f, 10000.f);
}

tg::mat4 dr::camera::get_view() { return tg::look_at_directx(position, target, tg::vec3(0, 1, 0)); }

tg::mat4 dr::camera::get_vp() { return projection * get_view(); }
