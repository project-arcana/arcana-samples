#include <nexus/monte_carlo_test.hh>

#include <clean-core/any_of.hh>
#include <clean-core/map.hh>

TEST("cc::map")
{
    cc::map<int, int> m;

    CHECK(m.empty());
    CHECK(m.size() == 0);

    m[7] = 3;
    CHECK(m.size() == 1);
    CHECK(m.contains_key(7));
    CHECK(!m.contains_key(5));

    CHECK(m.get(7) == 3);
    CHECK(m[7] == 3);

    m.clear();
    CHECK(m.size() == 0);
    CHECK(!m.contains_key(5));
    CHECK(!m.contains_key(7));

    m[3] = 4;
    m[1] = 5;
    auto cnt = 0;
    for (auto&& [k, v] : m)
    {
        ++cnt;
        CHECK(k == cc::any_of(1, 3));

        if (k == 3)
            CHECK(v == 4);
        if (k == 1)
            CHECK(v == 5);
    }
    CHECK(cnt == 2);

    for (auto&& [k, v] : m)
        v += k;
    CHECK(m[3] == 7);
    CHECK(m[1] == 6);

    for (auto k : m.keys())
    {
        CHECK(k == cc::any_of(3, 1));
        CHECK(m.contains_key(k));
    }
    for (auto const& v : m.values())
    {
        CHECK(v == cc::any_of(6, 7));
    }
    for (auto& v : m.values())
        v += 2;
    CHECK(m[3] == 9);
    CHECK(m[1] == 8);
}
