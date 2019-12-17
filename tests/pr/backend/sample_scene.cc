#include "sample_scene.hh"

void pr_test::fill_model_matrix_data(pr_test::model_matrix_data& data, double runtime, unsigned from, unsigned to)
{
    cc::array constexpr model_positions
        = {tg::vec3(1, 0, 0), tg::vec3(0, 1, 0), tg::vec3(0, 0, 1), tg::vec3(-1, 0, 0), tg::vec3(0, -1, 0), tg::vec3(0, 0, -1)};

    auto mp_i = from % model_positions.size();
    for (auto i = from; i < to; ++i)
    {
        data[i] = get_model_matrix(model_positions[mp_i] * 0.25f * float(i), runtime / (double(i + 1) * .15), i);

        ++mp_i;
        if (mp_i == model_positions.size())
            mp_i -= model_positions.size();
    }
}
