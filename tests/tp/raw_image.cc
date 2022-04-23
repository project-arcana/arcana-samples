#include <nexus/test.hh>

#include <texture-processor/raw_image.hh>

TEST("raw_image")
{
    auto img = tp::image2<float>::filled({4, 3}, 0.f);

    for (auto&& [p, v] : img)
        v = p.x + 10 * p.y;

    auto raw_img = tp::raw_image(img);

    CHECK(raw_img.metadata().extent.x == 4);
    CHECK(raw_img.metadata().extent.y == 3);
    CHECK(raw_img.metadata().channels == 1);
    CHECK(raw_img.metadata().byte_per_channel == 4);
    CHECK(raw_img.metadata().pixel_format == tp::pixel_format::f32);

    CHECK(raw_img.size_bytes() == sizeof(float) * 4 * 3);

    CHECK(raw_img.can_convert_to<decltype(img)>());
    CHECK(raw_img.can_view_as<tp::image2_view<float>>());

    {
        auto view = raw_img.view_as<tp::image2_view<float>>();

        CHECK(view.extent() == img.extent());
        for (auto p : img.positions())
            CHECK(view[p] == img[p]);
    }

    {
        auto view2 = raw_img.view_as<tp::image2_view<float const>>();

        CHECK(view2.extent() == img.extent());
        for (auto p : img.positions())
            CHECK(view2[p] == img[p]);
    }

    {
        // auto img2 = raw_img.convert_to<tp::image2<float>>();
        //
        // CHECK(img2.extent() == img.extent());
        // for (auto p : img.positions())
        //     CHECK(img2[p] == img[p]);
    }
}
