#include <nexus/monte_carlo_test.hh>

#include <iostream>

#include <polymesh/Mesh.hh>
#include <polymesh/debug.hh>
#include <polymesh/objects.hh>
#include <polymesh/properties.hh>

#include <typed-geometry/tg-std.hh>

namespace
{
struct Mesh3D
{
    pm::unique_ptr<pm::Mesh> mesh;
    pm::vertex_attribute<tg::pos3> pos;

    static Mesh3D create()
    {
        Mesh3D m;
        m.mesh = pm::Mesh::create();
        m.pos = {*m.mesh};
        return m;
    }

    Mesh3D copy() const
    {
        Mesh3D m;
        m.mesh = mesh->copy();
        m.pos = pos.copy_to(*m.mesh);
        return m;
    }

    void copy_from(Mesh3D const& m)
    {
        mesh->copy_from(*m.mesh);
        pos.copy_from(m.pos);
    }

private:
    Mesh3D() = default;
};
}

// TODO: fix this test!
MONTE_CARLO_TEST("pm::Mesh topology mct", disabled)
{
    auto const get_vertex = [](Mesh3D const& m, unsigned idx) -> pm::vertex_handle {
        if (m.mesh->vertices().empty())
            return pm::vertex_handle::invalid;
        tg::rng rng;
        rng.seed(idx);
        return m.mesh->vertices().random(rng);
    };
    auto const get_halfedge = [](Mesh3D const& m, unsigned idx) -> pm::halfedge_handle {
        if (m.mesh->halfedges().empty())
            return pm::halfedge_handle::invalid;
        tg::rng rng;
        rng.seed(idx);
        return m.mesh->halfedges().random(rng);
    };

    auto const is_vertex_collapsible = [&](Mesh3D const& m, unsigned idx) {
        auto v = get_vertex(m, idx);
        return v.is_valid() && (v.is_isolated() || !v.is_boundary());
    };
    // auto const is_vertex_valid = [&](Mesh3D const& m, unsigned idx) {
    //     auto v = get_vertex(m, idx);
    //     return v.is_valid() && !v.is_removed();
    // };
    auto const can_add_or_get_edge_vv = [&](Mesh3D const& m, unsigned ia, unsigned ib) {
        auto va = get_vertex(m, ia);
        auto vb = get_vertex(m, ib);
        return va.is_valid() && vb.is_valid() && pm::can_add_or_get_edge(va, vb);
    };
    auto const can_add_or_get_edge_hh = [&](Mesh3D const& m, unsigned ia, unsigned ib) {
        auto ha = get_halfedge(m, ia);
        auto hb = get_halfedge(m, ib);
        return ha.is_valid() && hb.is_valid() && pm::can_add_or_get_edge(ha, hb);
    };
    auto const has_vertices_rng = [&](Mesh3D const& m, tg::rng&) { return !m.mesh->vertices().empty(); };
    auto const has_edges_rng = [&](Mesh3D const& m, tg::rng&) { return !m.mesh->edges().empty(); };
    auto const random_permutation = [&](tg::rng& rng, int size) {
        std::vector<int> p;
        p.resize(size);
        for (auto i = 0; i < size; ++i)
            p[i] = i;
        std::shuffle(p.begin(), p.end(), rng);
        return p;
    };
    auto const add_triangle = [](Mesh3D& m) {
        auto v0 = m.mesh->vertices().add();
        auto v1 = m.mesh->vertices().add();
        auto v2 = m.mesh->vertices().add();
        m.mesh->faces().add(v0, v1, v2);
        m.pos[v0] = {1, 0, 0};
        m.pos[v1] = {0, 1, 0};
        m.pos[v2] = {0, 0, 1};
    };
    auto const split_edge = [&](Mesh3D& m, tg::rng& rng) {
        auto e = m.mesh->edges().random(rng);
        auto np = mix(m.pos[e.vertexA()], m.pos[e.vertexB()], .5f);
        auto nv = m.mesh->edges().split(m.mesh->edges().random(rng));
        m.pos[nv] = np;
    };

    // random unsigned
    addOp("gen uint", [](tg::rng& rng) { return unsigned(rng()); });

    // create and copy
    addOp("make mesh", [] { return Mesh3D::create(); }).execute_at_least(5);
    addOp("copy", [](Mesh3D const& m) { return m.copy(); });
    addOp("copy_from", [](Mesh3D& lhs, Mesh3D const& rhs) { lhs.copy_from(rhs); });

    // general
    addOp("clear", [](Mesh3D& m) { m.mesh->clear(); });
    addOp("shrink_to_fit", [](Mesh3D& m) { m.mesh->shrink_to_fit(); });
    addOp("reset", [](Mesh3D& m) { m.mesh->reset(); });
    addOp("compactify", [](Mesh3D& m) { m.mesh->compactify(); });

    // objects
    addOp("add cube", [](Mesh3D& m) { pm::objects::add_cube(*m.mesh, m.pos); });
    addOp("add triangle", add_triangle);

    // vertices
    addOp("add vertex", [](Mesh3D& m) { m.mesh->vertices().add(); });
    addOp("collapse vertex", [&](Mesh3D& m, unsigned idx) { m.mesh->vertices().collapse(get_vertex(m, idx)); }).when(is_vertex_collapsible);
    addOp("remove vertex", [&](Mesh3D& m, tg::rng& rng) { m.mesh->vertices().remove(m.mesh->vertices().random(rng)); }).when(has_vertices_rng);
    addOp("permute vertices", [&](Mesh3D& m, tg::rng& rng) { m.mesh->vertices().permute(random_permutation(rng, m.mesh->all_vertices().size())); });

    // edges
    addOp("add_or_get edge vv", [&](Mesh3D& m, unsigned va, unsigned vb) { m.mesh->edges().add_or_get(get_vertex(m, va), get_vertex(m, vb)); }).when(can_add_or_get_edge_vv);
    addOp("add_or_get edge hh", [&](Mesh3D& m, unsigned ha, unsigned hb) {
        m.mesh->edges().add_or_get(get_halfedge(m, ha), get_halfedge(m, hb));
    }).when(can_add_or_get_edge_hh);
    addOp("split edge", split_edge).when(has_edges_rng);

    // faces
    // TODO

    // halfedges
    // TODO

    // "pseudo invariants"
    addOp("edge exists", [](Mesh3D const& m, tg::rng& rng) {
        auto e = m.mesh->edges().random(rng);
        CHECK(pm::are_adjacent(e.vertexA(), e.vertexB()));
    }).when(has_edges_rng);

    // invariants
    addInvariant("consistent", [](Mesh3D const& m) { m.mesh->assert_consistency(); });
}
