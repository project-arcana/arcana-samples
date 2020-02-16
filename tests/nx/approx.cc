#include <nexus/fuzz_test.hh>
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

FUZZ_TEST("approx abs fuzz")(tg::rng& rng)
{
    auto v = uniform(rng, -100.f, 100.f);
    auto abs = uniform(rng, tg::abs(v) * 0.001f, tg::abs(v) * 0.2f);
    auto vv = v + uniform(rng, abs * -0.9f, abs * 0.9f);
    auto vvv = v + uniform(rng, abs * 1.1f, abs * 2.0f) * uniform(rng, {-1, 1});
    CHECK(vv == nx::approx(v).abs(abs));
    CHECK(vvv != nx::approx(v).abs(abs));
}

FUZZ_TEST("approx rel fuzz")(tg::rng& rng)
{
    auto v = uniform(rng, -100.f, 100.f);
    auto vv = v * uniform(rng, 0.9f, 1.1f);
    auto vvv0 = v * uniform(rng, 1.1f, 1.2f);
    auto vvv1 = v * uniform(rng, 0.8f, 0.9f);
    CHECK(vv == nx::approx(v).rel(0.15f));
    CHECK(vvv0 != nx::approx(v).rel(0.08f));
    CHECK(vvv1 != nx::approx(v).rel(0.08f));
    CHECK(-v != nx::approx(v).rel(0.15f));
    CHECK(-vv != nx::approx(v).rel(0.15f));
}

TEST("approx fail", should_fail)
{
    float f = 0;
    for (auto i = 0; i < 10; ++i)
        f += 0.1f;

    CHECK(1.f != nx::approx(1.f));
    CHECK(f != nx::approx(1.f));
    CHECK(f == nx::approx(1.1f));
    CHECK(f != nx::approx(1.1f).abs(0.2f));

    CHECK(0.99f == nx::approx(1.f));
    CHECK(-0.99f == nx::approx(1.f));
    CHECK(-1.f == nx::approx(1.f));
    CHECK(0.99f != nx::approx(1.f).abs(0.1f));
    CHECK(0.99f != nx::approx(1.f).rel(0.1f));
}

TEST("approx tg")
{
    auto v0 = tg::vec3(1, 2, 3);
    auto v1 = v0;
    auto r = tg::rotation_around(normalize(tg::vec3(8, 3, 2)), 10_deg);
    for (auto i = 0; i < 36; ++i)
        v1 = r * v1;

    CHECK(v1 != v0);
    CHECK(v1 == nx::approx(v0));
}
