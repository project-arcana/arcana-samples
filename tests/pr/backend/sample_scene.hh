#pragma once

#include <clean-core/array.hh>

#include <typed-geometry/tg.hh>

namespace pr_test
{
inline constexpr bool massive_sample = false;

inline constexpr float cam_dist = massive_sample ? 1000.f : 10.f;

inline auto const get_projection_matrix = [](int w, int h) -> tg::mat4 { return tg::perspective_directx(60_deg, w / float(h), 0.1f, 100000.f); };

inline auto const get_cam_pos = [](double runtime) -> tg::pos3 {
    return tg::rotate_y(tg::pos3(1, 1.5f, 1) * cam_dist, tg::radians(float(runtime * 0.05)))
           + tg::vec3(0, tg::sin(tg::radians(float(runtime * 0.125))) * cam_dist, 0);
};

inline auto const get_view_matrix = [](tg::pos3 const& cam_pos) -> tg::mat4 {
    constexpr auto target = tg::pos3(0, 1.45f, 0);
    return tg::look_at_directx(cam_pos, target, tg::vec3(0, 1, 0));
};

inline auto const get_view_projection_matrix
    = [](tg::pos3 const& cam_pos, int w, int h) -> tg::mat4 { return get_projection_matrix(w, h) * get_view_matrix(cam_pos); };


inline constexpr auto sample_mesh_path = massive_sample ? "res/pr/liveness_sample/mesh/icosphere.obj" : "res/pr/liveness_sample/mesh/ball.mesh";
inline constexpr auto sample_mesh_binary = massive_sample ? false : true;

inline constexpr auto sample_albedo_path = massive_sample ? "res/pr/liveness_sample/texture/uv_checker.png" : "res/pr/liveness_sample/texture/ball/albedo.png";
inline constexpr auto sample_normal_path = "res/pr/liveness_sample/texture/ball/normal.png";
inline constexpr auto sample_metallic_path = "res/pr/liveness_sample/texture/ball/metallic.png";
inline constexpr auto sample_roughness_path = "res/pr/liveness_sample/texture/ball/roughness.png";

struct global_data
{
    tg::mat4 cam_vp;
    tg::pos3 cam_pos;
    float runtime;
};

static constexpr unsigned num_instances = massive_sample ? 1000000 : 256;
using model_matrix_data = cc::array<tg::mat4, num_instances>;
void fill_model_matrix_data(model_matrix_data& data, double runtime, unsigned from, unsigned to);
}
