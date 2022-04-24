#include <nexus/test.hh>

#include <reflector/introspect.hh>
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

template <class In>
constexpr void introspect(In&& inspect, foo& v)
{
    inspect(v.a, "a");
    inspect(v.b, "b");
    inspect(v.c, "c");
}
}

TEST("reflector basics")
{
    // some static guarantees
    static_assert(rf::member_count<foo> == 3);
    static_assert(rf::is_introspectable<foo>);
    static_assert(!rf::is_introspectable<non_rf_class>);
    static_assert(rf::member_infos<foo>.size() == 3);

    auto const& members = rf::member_infos<foo>;
    CHECK(members[0].name == "a");
    CHECK(members[1].name == "b");
    CHECK(members[2].name == "c");
}
