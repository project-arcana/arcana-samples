#include <nexus/fuzz_test.hh>

FUZZ_TEST("fuzz test")(tg::rng& rng)
{
    CHECK(tg::abs(uniform(rng, -10.f, 10.f)) >= 0);

    auto a = uniform(rng, -10.f, 10.f);
    auto b = uniform(rng, -10.f, 10.f);
    CHECK(tg::abs(a + b) <= tg::abs(a) + tg::abs(b));
}
