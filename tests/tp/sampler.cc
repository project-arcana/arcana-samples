#include <nexus/test.hh>

#include <clean-core/vector.hh>

#include <texture-processor/image_view.hh>
#include <texture-processor/sampler.hh>

#include <typed-geometry/tg.hh>

TEST("sampler basics")
{
    auto data = cc::vector<int>{1, 2, 3, //
                                4, 5, 6};

    auto img = tp::image2_view<int>::from_data(cc::as_byte_span(data).data(), {3, 2}, {sizeof(int), 3 * sizeof(int)});

    CHECK(img(0, 0) == 1);
    CHECK(img(1, 0) == 2);
    CHECK(img(2, 0) == 3);
    CHECK(img(0, 1) == 4);
    CHECK(img(1, 1) == 5);
    CHECK(img(2, 1) == 6);

    auto sampler = tp::linear_clamped_px_sampler(img);

    CHECK(sampler({0, 0}) == 1);
    CHECK(sampler({1, 0}) == 2);
    CHECK(sampler({2, 0}) == 3);
    CHECK(sampler({0, 1}) == 4);
    CHECK(sampler({1, 1}) == 5);
    CHECK(sampler({2, 1}) == 6);

    CHECK(sampler({-10, 0}) == 1);
    CHECK(sampler({10, 0}) == 3);
    CHECK(sampler({-10, -10}) == 1);
    CHECK(sampler({10, -10}) == 3);
    CHECK(sampler({-10, 10}) == 4);
    CHECK(sampler({10, 10}) == 6);

    // NOTE: sampler keeps pixel type, thus interpolation casts to integer
}

TEST("sampler tg")
{
    auto data = cc::vector<tg::vec2>{{10, 5},
                                     {20, 3}, //
                                     {30, 8},
                                     {10, 2}};


    auto img = tp::image2_view<tg::vec2>::from_data(cc::as_byte_span(data).data(), {2, 2}, {sizeof(tg::vec2), 2 * sizeof(tg::vec2)});

    CHECK(img(0, 0) == tg::vec2(10, 5));
    CHECK(img(1, 0) == tg::vec2(20, 3));
    CHECK(img(0, 1) == tg::vec2(30, 8));
    CHECK(img(1, 1) == tg::vec2(10, 2));

    auto sampler = tp::linear_clamped_px_sampler(img);

    CHECK(sampler({0, 0}) == tg::vec2(10, 5));
    CHECK(sampler({1, 0}) == tg::vec2(20, 3));
    CHECK(sampler({0, 1}) == tg::vec2(30, 8));
    CHECK(sampler({1, 1}) == tg::vec2(10, 2));

    CHECK(sampler({0.5f, 0}) == tg::vec2(15, 4));
    CHECK(sampler({0.5f, 1}) == tg::vec2(20, 5));
    CHECK(sampler({0.5f, 0.5f}) == tg::vec2(17.5f, 4.5f));
}
