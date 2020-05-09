#include "scene.hh"

#include <phantasm-renderer/Context.hh>

#include <typed-geometry/tg.hh>

#include <arcana-incubator/device-abstraction/freefly_camera.hh>

void dmr::scene::init(pr::Context& ctx, unsigned max_num_instances)
{
    num_backbuffers = ctx.get_num_backbuffers();

    instances.reserve(max_num_instances);
    instance_transforms.reserve(max_num_instances);

    per_frame_resources.emplace(num_backbuffers);
    for (auto& per_frame : per_frame_resources)
    {
        per_frame.cb_camdata = ctx.make_upload_buffer(sizeof(scene_gpudata));
        per_frame.sb_modeldata = ctx.make_upload_buffer(sizeof(instance_gpudata) * max_num_instances, sizeof(instance_gpudata));
    }

    current_frame_index = 0;
}

void dmr::scene::on_next_frame()
{
    current_frame_index = cc::wrapped_increment(current_frame_index, num_backbuffers);
    halton_index = cc::wrapped_increment(halton_index, 8u);
    is_history_a = !is_history_a;
}

void dmr::scene::upload_current_frame(pr::Context& ctx)
{
    auto& frame = current_frame();

    ctx.write_to_buffer_t(frame.cb_camdata, camdata);
    ctx.write_to_buffer_t(frame.sb_modeldata, instance_transforms);
}

void dmr::scene_gpudata::fill_data(tg::isize2 res, tg::pos3 campos, tg::vec3 camforward, unsigned halton_index)
{
    auto const clean_proj = tg::perspective_reverse_z_directx(60_deg, res.width / float(res.height), 0.1f);

    auto const jitter_x = (inc::da::halton_sequence(halton_index, 2) - 0.5f) / float(res.width);
    auto const jitter_y = (inc::da::halton_sequence(halton_index, 3) - 0.5f) / float(res.height);

    proj = clean_proj;
    proj[2][0] = jitter_x;
    proj[2][1] = jitter_y;
    proj_inv = tg::inverse(proj);

    view = tg::look_at_directx(campos, camforward, tg::vec3(0, 1, 0));
    view_inv = tg::inverse(view);

    vp = proj * view;
    vp_inv = tg::inverse(vp);

    prev_clean_vp = clean_vp;
    prev_clean_vp_inv = clean_vp_inv;

    clean_vp = clean_proj * view;
    clean_vp_inv = tg::inverse(clean_vp);
}
