#include <nexus/fuzz_test.hh>

#include <nexus/detail/trace_serialize.hh>

#include <clean-core/span.hh>
#include <clean-core/string.hh>
#include <clean-core/vector.hh>

FUZZ_TEST("trace_serialize")(tg::rng& rng)
{
    cc::vector<int> v;
    auto cnt = uniform(rng, 0, 30);
    for (auto i = 0; i < cnt; ++i)
        v.push_back(uniform(rng, {-1, uniform(rng, 0, 10), uniform(rng, 0, 1000), uniform(rng, 0, 10000)}));

    auto s = nx::detail::trace_encode(v);
    auto v2 = nx::detail::trace_decode(s);

    CHECK(v == v2);
}
