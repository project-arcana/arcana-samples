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

    CHECK(m.size() == 2);
    CHECK(m.remove_key(2) == false);

    CHECK(m.size() == 2);
    CHECK(m.remove_key(1) == true);
    CHECK(m.size() == 1);
    CHECK(!m.contains_key(1));
    CHECK(m.contains_key(3));
    CHECK(m.remove_key(1) == false);
    CHECK(m.remove_key(3) == true);
    CHECK(m.empty());

    m = {{10, 7}, {12, 8}};
    CHECK(m.size() == 2);
    CHECK(m[10] == 7);
    CHECK(m[12] == 8);
    CHECK(m == cc::map<int, int>{{10, 7}, {12, 8}});
    CHECK(m == cc::map<int, int>{{12, 8}, {10, 7}});
}

TEST("cc::map pointer stability")
{
    cc::map<int, int> m;

    m[0] = 17;
    auto p = &m[0];

    for (auto i = 0; i < 1000; ++i)
    {
        m[100 + i] = i;
        m[0] = i;
        CHECK(*p == i);
    }
}
