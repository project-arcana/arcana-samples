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

void dr::scene::on_next_frame() { current_frame_index = cc::wrapped_increment(current_frame_index, num_backbuffers); }

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

void dr::scene_gpudata::fill_data(tg::isize2 res, tg::pos3 campos, tg::vec3 camforward)
{
    prev_clean_proj = clean_proj;
    clean_proj = tg::perspective_reverse_z_directx(60_deg, res.width / float(res.height), 0.1f);

    proj = clean_proj; // TODO jitter
    proj_inv = tg::inverse(proj);

    view = tg::look_at_directx(campos, camforward, tg::vec3(0, 1, 0));
    view_inv = tg::inverse(view);

    vp = proj * view;
    vp_inv = tg::inverse(vp);

    cam_pos = campos;
    runtime = 0.f;
}
