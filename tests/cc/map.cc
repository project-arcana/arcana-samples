#include <nexus/monte_carlo_test.hh>

#include <clean-core/map.hh>

TEST("cc::map")
{
    cc::map<int, int> m;

    CHECK(m.empty());
    CHECK(m.size() == 0);

#ifndef CC_OS_WINDOWS
    m[7] = 3;
    CHECK(m.size() == 1);
    CHECK(m.contains_key(7));
    CHECK(!m.contains_key(5));

    CHECK(m.get(7) == 3);
    CHECK(m[7] == 3);
#endif
}
