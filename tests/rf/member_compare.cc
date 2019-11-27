#include <nexus/test.hh>

#include <clean-core/string.hh>

#include <reflector/compare.hh>

namespace
{
struct bar
{
    int x;
    char _padding[14];
    int y;
    alignas (std::max_align_t) char _aligned_padding[3];
    void* z;
};
template <class I>
void introspect(I&& i, bar& v)
{
    i(v.x, "x");
    i(v.y, "y");
    i(v.z, "z");
}
}

TEST("rf::member_compare")
{
    bar b1;
    b1.x = 5;
    b1.y = 51;
    b1.z = nullptr;
    CHECK(rf::is_equal(b1, b1));

    auto b2 = b1;
    CHECK(rf::is_equal(b1, b2));
    CHECK(rf::is_equal(b2, b1));

    b2.z = &b1;
    CHECK(!rf::is_equal(b1, b2));
    CHECK(!rf::is_equal(b2, b1));

    auto const b3 = b2;
    CHECK(rf::is_equal(b3, b2));
    CHECK(rf::is_equal(b2, b3));

    CHECK(rf::is_less(0, 5));
    CHECK(rf::is_greater(6, 5));
    CHECK(rf::is_equal(0, 0));
    CHECK(rf::is_not_equal(1, 0));
    CHECK(rf::is_greater_equal(0, 0));
    CHECK(rf::is_less_equal(0, 0));
    CHECK(rf::is_less_equal(-1, 0));
}
