#include "scene.hh"

#include <cmath>

#include <clean-core/utility.hh>

#include "sample_util.hh"

namespace
{
tg::vec3 index_mod_vec(float index, tg::vec3 mod) { return tg::vec3(std::fmod(index, mod.x), std::fmod(index, mod.y), std::fmod(index, mod.z)); }
tg::vec3 compwise_product(tg::vec3 const& lhs, tg::vec3 const& rhs) { return tg::vec3(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z); }

inline constexpr float model_scale = phi_test::massive_sample ? 10.f : 0.21f;

inline tg::mat4 get_model_matrix(tg::vec3 pos_base, float runtime, unsigned index, tg::vec3 pos_mul)
{
    if constexpr (phi_test::massive_sample)
    {
        return tg::translation(pos_base + tg::vec3(float(index % 128), float(index % 36), float(index % 256))
                               + tg::vec3(float(index % 3), float(index % 5), float(index % 7)) * 100.f
                               /*+ tg::vec3(float(index % 3), float(index % 5), float(index % 7)) * 100.f*/)
               * tg::rotation_y(tg::radians((float(runtime * 2.f) + float(index) * 0.5f * tg::pi_scalar<float>)*(index % 2 == 0 ? -1 : 1)))
               * tg::scaling(model_scale, model_scale, model_scale);
    }
    else
    {
        auto const pos = pos_base + index_mod_vec(float(index), pos_mul);

        return tg::translation(pos)
               * tg::rotation_y(tg::radians((float(runtime * 2.f) + float(index) * 0.125f * tg::pi_scalar<float>)*(index % 2 == 0 ? -1 : 1)))
               * tg::scaling(model_scale, model_scale, model_scale);
    }
};

constexpr tg::uvec3 gc_massive_cube_dims = tg::uvec3(100, 100, 100);
constexpr float gc_massive_cube_scale = 75.f;
}

void phi_test::fill_model_matrix_data(cc::span<tg::mat4> data, float runtime, unsigned from, unsigned to, tg::vec3 mul)
{
    cc::array constexpr model_positions
        = {tg::vec3(1, 0, 0), tg::vec3(0, 1, 0), tg::vec3(0, 0, 1), tg::vec3(-1, 0, 0), tg::vec3(0, -1, 0), tg::vec3(0, 0, -1)};

    auto mp_i = from % model_positions.size();
    for (auto i = from; i < to; ++i)
    {
        if constexpr (phi_test::massive_sample)
        {
            constexpr auto dimensionality = tg::vec<4, size_t>{gc_massive_cube_dims.x, gc_massive_cube_dims.y, gc_massive_cube_dims.z, 1};
            auto const dim = tg::vec4(phi_test::dimensional_index(dimensionality, i)) * gc_massive_cube_scale;
            data[i] = tg::translation(compwise_product(tg::vec3(dim.x, dim.y, dim.z), mul))
                      * tg::rotation_y(tg::radians((float(runtime * 2.f) + float(i) * 0.125f * tg::pi_scalar<float>)*(i % 2 == 0 ? -1 : 1)))
                      * tg::scaling(model_scale, model_scale, model_scale);
        }
        else
        {
            data[i] = get_model_matrix(model_positions[mp_i] * 0.25f * float(i), runtime /* / (float(i + 1) * .15f)*/, i, mul);

            mp_i = cc::wrapped_increment(mp_i, model_positions.size());
        }
    }
}
