#include <doctest.hh>

#include <clean-core/array.hh>

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

TEST_CASE("fixed cc::array")
{
    cc::array<int, 3> a;

    a = cc::make_array(1, 2, 3);
    CHECK(tg::sum(a) == 6);
}
