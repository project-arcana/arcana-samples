#include <nexus/fuzz_test.hh>
#include <nexus/test.hh>

#include <clean-core/from_string.hh>

TEST("cc::from_string")
{
    int v;
    CHECK(cc::from_string("123", v));
    CHECK(v == 123);
}

FUZZ_TEST("cc::from_string fuzz")(tg::rng& rng)
{
    auto check_range = [&](auto min, auto max) {
        auto v0 = min;
        auto v1 = min;
        for (auto i = 0; i < 100; ++i)
        {
            v0 = uniform(rng, min, max);
            auto s = cc::to_string(v0);
            auto ok = cc::from_string(s, v1);
            CHECK(ok);
            CHECK(v0 == v1);
        }
    };
    auto check_range_near = [&](auto min, auto max, auto tol) {
        auto v0 = min;
        auto v1 = min;
        for (auto i = 0; i < 100; ++i)
        {
            v0 = uniform(rng, min, max);
            auto s = cc::to_string(v0);
            auto ok = cc::from_string(s, v1);
            CHECK(ok);
            CHECK(v0 == nx::approx(v1).abs(tol));
        }
    };

    check_range(-100, 100);
    check_range(-100000000000, 100000000000);
    check_range(5u, 100u);
    check_range(5uLL, 100uLL);
    check_range_near(-100., 100., 0.01);
    check_range_near(-100.f, 100.f, 0.01f);
}
