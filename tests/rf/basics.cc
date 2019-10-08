#include <doctest.hh>

#include <reflector/members.hh>

namespace
{
struct foo
{
    int a;
    char b;
    bool c;
};
struct non_rf_class
{
    int a, b, c;
};

template <class I>
constexpr void introspect(I&& i, foo& v)
{
    i(v.a, "a");
    i(v.b, "b");
    i(v.c, "c");
}
}

TEST_CASE("reflector basics")
{
    // some static guarantees
    static_assert(rf::member_count<foo> == 3);
    static_assert(rf::is_introspectable<foo>);
    static_assert(!rf::is_introspectable<non_rf_class>);
    // TODO! static_assert(rf::member_infos<foo>.size() == 3);
}
