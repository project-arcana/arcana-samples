#include <nexus/test.hh>

#include <texture-processor/image.hh>
#include <texture-processor/image_view.hh>
#include <texture-processor/io.hh>

#include <rich-log/log.hh>

TEST("tp api")
{
    // normal 2D texture
    {
        auto img = tp::image2<float>::filled({128, 256}, 1.0f);
        img(7, 14) = 2;
        CHECK(img(7, 13) == 1.0f);
        CHECK(img(7, 14) == 2.0f);
        CHECK(img.has_natural_stride());

        // and its view
        auto view = img.view();
        CHECK(view(7, 13) == 1.0f);
        CHECK(view(7, 14) == 2.0f);
        CHECK(view.has_natural_stride());
        view = view.subview({2, 1}, {50, 60});
        CHECK(view(5, 13) == 2.0f);
        CHECK(!view.has_natural_stride());

        // for (auto& v : img.pixels())
        //     v = 0.5f;
        // CHECK(view(2, 7) == 0.5f);
    }
}
