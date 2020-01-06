#include <nexus/test.hh>

#include <typed-geometry/tg-lean.hh>

#include <reflector/compare.hh>

namespace tg
{
template <class I>
void introspect(I&& i, pos3 const& p)
{
    i(p.x, "x");
    i(p.y, "y");
    i(p.z, "z");
}
}

namespace
{
struct edge
{
    tg::pos3 a;
    tg::pos3 b;
};
template <class I>
void introspect(I&& i, edge const& e)
{
    i(e.a, "a");
    i(e.b, "b");
}
}

TEST("recursive compare")
{
    edge e0, e1;

    CHECK(rf::is_equal(e0, e1));
    CHECK(rf::equal()(e0, e1));
}
