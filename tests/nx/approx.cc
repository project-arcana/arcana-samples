#include <nexus/test.hh>

#include <nexus/ext/tg-approx.hh>

#include <typed-geometry/tg.hh>

TEST("approx")
{
    float f = 0;
    for (auto i = 0; i < 10; ++i)
        f += 0.1f;

    CHECK(f == nx::approx(1.f));
    CHECK(f != nx::approx(1.1f));
    CHECK(f == nx::approx(1.1f).abs(0.2f));

    CHECK(0.99f != nx::approx(1.f));
    CHECK(0.99f == nx::approx(1.f).abs(0.1f));
    CHECK(0.99f == nx::approx(1.f).rel(0.1f));
}

TEST("approx", should_fail)
{
    float f = 0;
    for (auto i = 0; i < 10; ++i)
        f += 0.1f;

    CHECK(1.f != nx::approx(1.f));
    CHECK(f != nx::approx(1.f));
    CHECK(f == nx::approx(1.1f));
    CHECK(f != nx::approx(1.1f).abs(0.2f));

    CHECK(0.99f == nx::approx(1.f));
    CHECK(0.99f != nx::approx(1.f).abs(0.1f));
    CHECK(0.99f != nx::approx(1.f).rel(0.1f));
}

TEST("approx tg")
{
    auto v0 = tg::vec3(1,2,3);
    auto v1 = v0;
    auto r = tg::rotation_around(normalize(tg::vec3(8,3,2)), 10_deg);
    for (auto i = 0; i < 36; ++i)
        v1 = r * v1;

    CHECK(v1 != v0);
    CHECK(v1 == nx::approx(v0));
}
