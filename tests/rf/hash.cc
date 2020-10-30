#include <nexus/test.hh>

#include <reflector/hash.hh>

namespace
{
struct foo
{
    int a = 123;
    bool b = false;
};
template <class In>
constexpr void introspect(In&& inspect, foo& v)
{
    inspect(v.a, "a");
    inspect(v.b, "b");
}
}

TEST("rf::hash")
{
    CHECK(rf::make_hash(1234) == cc::make_hash(1234));
    CHECK(rf::make_hash(1234, true, 13.4f) == cc::make_hash(1234, true, 13.4f));

    foo f;
    static_assert(!cc::can_hash<foo>);
    static_assert(rf::can_hash<foo>);

    CHECK(rf::make_hash(f) == cc::hash_combine(cc::hash_combine(), f.a, f.b));
}
