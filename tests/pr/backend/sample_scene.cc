#include "sample_scene.hh"

namespace
{
inline constexpr float model_scale = pr_test::massive_sample ? 10.f : 0.21f;
inline auto const get_model_matrix = [](tg::vec3 pos, float runtime, unsigned index) -> tg::mat4 {
    if constexpr (pr_test::massive_sample)
    {
        return tg::translation(pos + tg::vec3(float(index % 128), float(index % 36), float(index % 256))
                               + tg::vec3(float(index % 3), float(index % 5), float(index % 7)) * 100.f
                               /*+ tg::vec3(float(index % 3), float(index % 5), float(index % 7)) * 100.f*/)
               * tg::rotation_y(tg::radians((float(runtime * 2.f) + float(index) * 0.5f * tg::pi_scalar<float>)*(index % 2 == 0 ? -1 : 1)))
               * tg::scaling(model_scale, model_scale, model_scale);
    }
    else
    {
        return tg::translation(pos + tg::vec3(float(index % 9), float(index % 6), float(index % 9)))
               * tg::rotation_y(tg::radians((float(runtime * 2.f) + float(index) * 0.5f * tg::pi_scalar<float>)*(index % 2 == 0 ? -1 : 1)))
               * tg::scaling(model_scale, model_scale, model_scale);
    }
};
}

void pr_test::fill_model_matrix_data(pr_test::model_matrix_data& data, float runtime, unsigned from, unsigned to)
{
    cc::array constexpr model_positions
        = {tg::vec3(1, 0, 0), tg::vec3(0, 1, 0), tg::vec3(0, 0, 1), tg::vec3(-1, 0, 0), tg::vec3(0, -1, 0), tg::vec3(0, 0, -1)};

    auto mp_i = from % model_positions.size();
    for (auto i = from; i < to; ++i)
    {
        data[i] = get_model_matrix(model_positions[mp_i] * 0.25f * float(i), runtime / (float(i + 1) * .15f), i);

        ++mp_i;
        if (mp_i == model_positions.size())
            mp_i -= model_positions.size();
    }
}
