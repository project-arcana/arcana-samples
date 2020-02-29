#include <nexus/monte_carlo_test.hh>

#include <clean-core/set.hh>

TEST("cc::set")
{
    bool b;
    cc::set<int> s;

    CHECK(s.empty());
    CHECK(s.size() == 0);
    CHECK(!s.contains(3));

    s.add(3);
    CHECK(s.size() == 1);
    CHECK(s.contains(3));
    CHECK(!s.contains(4));

    s.add(3);
    CHECK(s.size() == 1);

    s.add(5);
    CHECK(s.size() == 2);

    b = s.remove(7);
    CHECK(!b);
    CHECK(s.size() == 2);

    b = s.remove(3);
    CHECK(b);
    CHECK(s.size() == 1);
    CHECK(!s.contains(3));

    b = s.remove(5);
    CHECK(b);
    CHECK(s.size() == 0);
    CHECK(!s.contains(5));
}
