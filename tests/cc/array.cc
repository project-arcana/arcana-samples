#include <nexus/test.hh>

#include <clean-core/array.hh>
#include <clean-core/assert.hh>

#include <typed-geometry/tg.hh>

TEST("cc::array")
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

TEST("fixed cc::array")
{
    cc::array<int, 3> a;

    a = cc::make_array(1, 2, 3);
    CHECK(tg::sum(a) == 6);

    // Test if initializer lists create fixed cc::arrays
    {
        cc::array list = {1, 2, 3};
        cc::array list_single = {1};
        cc::array list_single_size_t = {size_t(42)};

        static_assert(std::is_same_v<decltype(list), cc::array<int, 3>>);
        static_assert(std::is_same_v<decltype(list_single), cc::array<int, 1>>);
        static_assert(std::is_same_v<decltype(list_single_size_t), cc::array<size_t, 1>>);
    }
}
