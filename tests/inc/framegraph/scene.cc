#include "scene.hh"

#include <phantasm-renderer/Context.hh>
#include <phantasm-renderer/Frame.hh>

#include <typed-geometry/tg.hh>

#include <arcana-incubator/device-abstraction/freefly_camera.hh>

void inc::scene::init(pr::Context& ctx, unsigned max_num_instances)
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

void inc::scene::on_next_frame()
{
    current_frame_index = cc::wrapped_increment(current_frame_index, num_backbuffers);
    halton_index = cc::wrapped_increment(halton_index, 8u);
    is_history_a = !is_history_a;
}

void inc::scene::upload_current_frame(pr::Context& ctx)
{
    auto& frame = current_frame();

    ctx.write_to_buffer(frame.cb_camdata, camdata);
    ctx.write_to_buffer(frame.sb_modeldata, instance_transforms);
}

void inc::scene_gpudata::fill_data(tg::isize2 res, tg::pos3 campos, tg::vec3 camforward, unsigned halton_index)
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

inc::mesh inc::asset_pack::loadMesh(pr::Context& ctx, const char* path, bool binary)
{
    auto const& new_mesh = unique_meshes.push_back(inc::pre::load_mesh(ctx, path, binary));
    mesh res;
    res.vertex = new_mesh.vertex;
    res.index = new_mesh.index;
    return res;
}

inc::material inc::asset_pack::loadMaterial(pr::Context& ctx, inc::pre::texture_processing& tex, const char* p_albedo, const char* p_normal, const char* p_arm)
{
    auto frame = ctx.make_frame();

    auto albedo = tex.load_texture(frame, p_albedo, pr::format::rgba8un, true, true);
    auto normal = tex.load_texture(frame, p_normal, pr::format::rgba8un, true, false);
    auto ao_rough_metal = tex.load_texture(frame, p_arm, pr::format::rgba8un, true, false);

    frame.transition(albedo, pr::state::shader_resource, pr::shader::pixel);
    frame.transition(normal, pr::state::shader_resource, pr::shader::pixel);
    frame.transition(ao_rough_metal, pr::state::shader_resource, pr::shader::pixel);

    ctx.submit(cc::move(frame));

    auto const& new_sv = unique_svs.push_back(ctx.build_argument().add(albedo).add(normal).add(ao_rough_metal).make_graphics());
    material res;
    res.outer_sv = new_sv;

    // move to unique array to keep alive
    unique_textures.push_back(cc::move(albedo));
    unique_textures.push_back(cc::move(normal));
    unique_textures.push_back(cc::move(ao_rough_metal));

    return res;
}
