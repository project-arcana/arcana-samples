#include <nexus/test.hh>

#include <texture-processor/image.hh>

TEST("strided views: sub view")
{
    auto img = tp::image2<float>::filled({4, 3}, 0.f);

    for (auto&& [p, v] : img)
        v = p.x + 10 * p.y;

    CHECK(img(1, 2) == 21);
    CHECK(img.is_on_boundary({1, 2}));

    auto v = img.view();
    CHECK(v(1, 2) == 21);
    v = v.subview({1, 2}, {2, 1});
    CHECK(v.width() == 2);
    CHECK(v.height() == 1);

    v = img;
    CHECK(v(1, 2) == 21);
    CHECK(v.width() == 4);
    CHECK(v.height() == 3);

    v = v.mirrored<1>();
    CHECK(v.width() == 4);
    CHECK(v.height() == 3);
    CHECK(v(1, 2) == 1);

    v = v.mirrored<0>();
    CHECK(v.width() == 4);
    CHECK(v.height() == 3);
    CHECK(v(1, 2) == 2);

    v = v.mirrored_x().mirrored_y();
    CHECK(v(1, 2) == 21);
    CHECK(v.width() == 4);
    CHECK(v.height() == 3);

    v = v.swapped<0, 1>();
    CHECK(v(1, 2) == 12);
    CHECK(v.width() == 3);
    CHECK(v.height() == 4);

    // TODO: implement me
    // auto r = v.row(1);
}
