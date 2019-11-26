#include <nexus/test.hh>

#include <clean-core/string.hh>

#include <reflector/member_compare.hh>

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
    CHECK(rf::member_compare(b1, b1));

    auto b2 = b1;
    CHECK(rf::member_compare(b1, b2));
    CHECK(rf::member_compare(b2, b1));

    b2.z = &b1;
    CHECK(!rf::member_compare(b1, b2));
    CHECK(!rf::member_compare(b2, b1));

    auto const b3 = b2;
    CHECK(rf::member_compare(b3, b2));
    CHECK(rf::member_compare(b2, b3));
}
