#include <nexus/test.hh>

#include <typed-geometry/tg-lean.hh>

#include <reflector/compare.hh>

namespace tg
{
template <class In>
void introspect(In&& inspect, pos3 const& p)
{
    inspect(p.x, "x");
    inspect(p.y, "y");
    inspect(p.z, "z");
}
}

namespace
{
struct edge
{
    tg::pos3 a;
    tg::pos3 b;
};
template <class In>
void introspect(In&& inspect, edge const& e)
{
    inspect(e.a, "a");
    inspect(e.b, "b");
}
}

TEST("recursive compare")
{
    edge e0, e1;

    CHECK(rf::is_equal(e0, e1));
    CHECK(rf::equal()(e0, e1));
}
