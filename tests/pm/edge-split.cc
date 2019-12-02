#include <nexus/test.hh>

#include <polymesh/Mesh.hh>

TEST("edge split bug")
{
    pm::Mesh m;

    auto v0 = m.vertices().add();
    auto v1 = m.vertices().add();
    auto v2 = m.vertices().add();

    auto e = m.edges().add_or_get(v0, v1);
    m.edges().add_or_get(v1, v2);

    m.edges().split(e);

    m.assert_consistency();
}
