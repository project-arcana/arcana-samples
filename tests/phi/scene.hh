#pragma once

#include <clean-core/array.hh>

#include <clean-core/span.hh>

#include <typed-geometry/tg.hh>

namespace phi_test
{
inline constexpr bool massive_sample = false;

inline constexpr auto sample_mesh_path = massive_sample ? "res/arcana-sample-resources/phi/mesh/icosphere.obj" : "res/arcana-sample-resources/phi/mesh/ball.mesh";
inline constexpr auto sample_mesh_binary = massive_sample ? false : true;

inline constexpr auto sample_albedo_path
    = massive_sample ? "res/arcana-sample-resources/phi/texture/uv_checker.png" : "res/arcana-sample-resources/phi/texture/ball/albedo.png";
inline constexpr auto sample_normal_path = "res/arcana-sample-resources/phi/texture/ball/normal.png";
inline constexpr auto sample_arm_path = "res/arcana-sample-resources/phi/texture/ball/arm.png";

void fill_model_matrix_data(cc::span<tg::mat4> data, float runtime, unsigned from, unsigned to, tg::vec3 mul);
}
