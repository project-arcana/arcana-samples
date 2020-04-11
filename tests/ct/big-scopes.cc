#include <nexus/test.hh>

#include <ctracer/scope.hh>
#include <ctracer/trace.hh>

TEST("ct::scope")
{
    auto const cnt = 1'000'000;

    ct::scope s;
    for (auto i = 0; i < cnt; ++i)
    {
        TRACE("inner");
    }

    CHECK(s.trace().compute_events().size() == cnt * 2);
}

TEST("ct::scope + inner null")
{
    auto const cntA = 10;
    auto const cntB = 10'000'000;

    ct::scope s;
    for (auto i = 0; i < cntA; ++i)
    {
        TRACE("inner");

        ct::null_scope ns;
        for (auto i = 0; i < cntB; ++i)
        {
            TRACE("null");
        }
    }

    CHECK(s.trace().compute_events().size() == cntA * 2);
}
