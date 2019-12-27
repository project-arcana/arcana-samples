#include <nexus/test.hh>

#include <polymesh/Mesh.hh>
#include <polymesh/algorithms/properties.hh>

TEST("polymesh triangle collapse A")
{
    pm::Mesh m;
    auto v0 = m.vertices().add();
    auto v1 = m.vertices().add();
    auto v2 = m.vertices().add();
    m.faces().add(v0, v1, v2);

    auto h = m.halfedges().find(v0, v1);
    CHECK(pm::can_collapse(h));
    CHECK(!h.is_boundary());

    m.halfedges().collapse(h);

    m.assert_consistency();
}

TEST("polymesh triangle collapse B")
{
    pm::Mesh m;
    auto v0 = m.vertices().add();
    auto v1 = m.vertices().add();
    auto v2 = m.vertices().add();
    m.faces().add(v0, v1, v2);

    auto h = m.halfedges().find(v1, v0);
    CHECK(pm::can_collapse(h));
    CHECK(h.is_boundary());

    m.halfedges().collapse(h);

    m.assert_consistency();
}
