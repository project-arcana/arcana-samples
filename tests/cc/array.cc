#include <doctest.hh>

#include <cc/array.hh>

#include <typed-geometry/tg.hh>

TEST_CASE("cc::array")
{
    cc::array<int> a;
    CHECK(a.empty());

    a = {1, 2, 3};
    CHECK(a.size() == 3);

    CHECK(tg::sum(a) == 6);

    cc::array<int> b = a;
    CHECK(a == b);

    b[1] = 7;
    CHECK(a != b);

    b = std::move(a);
    CHECK(a.empty());
    CHECK(tg::sum(b) == 6);
}