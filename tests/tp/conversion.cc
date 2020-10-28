#include <nexus/test.hh>

#include <typed-geometry/feature/vector.hh>

#include <texture-processor/image.hh>

TEST("image conversion")
{
    CHECK(tp::detail::is_normalized_u8<tg::u8vec3>());

    {
        auto imgA = tp::image2<tg::u8vec3>::filled({2, 2}, {255, 0, 255});

        CHECK(imgA(0, 0) == tg::u8vec3(255, 0, 255));

        auto imgB = tp::image2<tg::vec3>::defaulted({2, 2});
        imgA.copy_to(imgB);

        CHECK(imgB(0, 0) == tg::vec3(1, 0, 1));
    }

    {
        auto imgA = tp::image2<tg::u8>::filled({2, 2}, 255);

        CHECK(imgA(0, 0) == 255);

        auto imgB = tp::image2<tg::vec3>::defaulted({2, 2});
        imgA.copy_to(imgB);

        CHECK(imgB(0, 0) == tg::vec3(1, 1, 1));
    }

    {
        auto imgA = tp::image2<float>::filled({2, 2}, 1.0f);

        CHECK(imgA(0, 0) == 1.0f);

        auto imgB = tp::image2<tg::u8vec2>::defaulted({2, 2});
        imgA.copy_to(imgB);

        CHECK(imgB(0, 0) == tg::u8vec2(255, 255));
    }
}
