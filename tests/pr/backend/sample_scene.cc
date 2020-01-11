#include "sample_scene.hh"

#include <cmath>

namespace
{
tg::vec3 index_mod_vec(float index, tg::vec3 mod) { return tg::vec3(std::fmod(index, mod.x), std::fmod(index, mod.y), std::fmod(index, mod.z)); }
tg::vec3 inner_product(tg::vec3 const& lhs, tg::vec3 const& rhs) { return tg::vec3(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z); }

inline constexpr float model_scale = pr_test::massive_sample ? 10.f : 0.21f;
inline auto const get_model_matrix = [](tg::vec3 pos_base, float runtime, unsigned index, tg::vec3 pos_mul) -> tg::mat4 {
    if constexpr (pr_test::massive_sample)
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
               * tg::rotation_y(tg::radians((float(runtime * 2.f) + float(index) * 0.5f * tg::pi_scalar<float>)*(index % 2 == 0 ? -1 : 1)))
               * tg::scaling(model_scale, model_scale, model_scale);
    }
};
}

void pr_test::fill_model_matrix_data(pr_test::model_matrix_data& data, float runtime, unsigned from, unsigned to, tg::vec3 mul)
{
    cc::array constexpr model_positions
        = {tg::vec3(1, 0, 0), tg::vec3(0, 1, 0), tg::vec3(0, 0, 1), tg::vec3(-1, 0, 0), tg::vec3(0, -1, 0), tg::vec3(0, 0, -1)};

    auto mp_i = from % model_positions.size();
    for (auto i = from; i < to; ++i)
    {
        data[i] = get_model_matrix(model_positions[mp_i] * 0.25f * float(i), runtime / (float(i + 1) * .15f), i, mul);

        ++mp_i;
        if (mp_i == model_positions.size())
            mp_i -= model_positions.size();
    }
}
