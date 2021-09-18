#include <nexus/test.hh>

#include <polymesh/Mesh.hh>
#include <polymesh/algorithms.hh>
#include <polymesh/objects/cube.hh>

#include <typed-geometry/tg.hh>

TEST("pm pretty printer")
{
    pm::Mesh m;
    auto pos = m.vertices().make_attribute<tg::pos3>();
    pm::objects::add_cube(m, pos);
    auto normals = pm::face_normals(pos);
    auto edge_lengths = m.edges().map([&](auto e) { return pm::edge_length(e, pos); });
    auto half_third = m.halfedges().map([&](pm::halfedge_handle h) { return mix(pos[h.vertex_from()], pos[h.vertex_to()], 2 / 3.f); });
    auto vnormals = pm::vertex_normals_by_area(pos);

    auto vh0 = m.vertices()[0];
    auto vi0 = vh0.idx;
    auto vii = pm::vertex_index::invalid;
    auto vhi = pm::vertex_handle::invalid;
    auto vhr = m.vertices()[1];
    auto vhoob = pm::vertex_handle(&m, pm::vertex_index{999});

    auto eh0 = m.edges()[0];
    auto ei0 = eh0.idx;
    auto eii = pm::edge_index::invalid;
    auto ehi = pm::edge_handle::invalid;
    auto ehr = vhr.any_edge();
    auto ehoob = pm::edge_handle(&m, pm::edge_index{999});

    auto hh0 = m.halfedges()[0];
    auto hi0 = hh0.idx;
    auto hii = pm::halfedge_index::invalid;
    auto hhi = pm::halfedge_handle::invalid;
    auto hhr = vhr.any_outgoing_halfedge();
    auto hhoob = pm::halfedge_handle(&m, pm::halfedge_index{999});

    auto fh0 = m.faces()[0];
    auto fi0 = fh0.idx;
    auto fii = pm::face_index::invalid;
    auto fhi = pm::face_handle::invalid;
    auto fhr = vhr.any_face();
    auto fhoob = pm::face_handle(&m, pm::face_index{999});

    auto v_isolated = m.vertices().add();
    auto e_isolated = m.edges().add_or_get(m.vertices().add(), m.vertices().add());
    auto f_tri = m.faces().add(m.vertices().add(), m.vertices().add(), m.vertices().add());

    m.vertices().remove(vhr);


    CHECK(true); // not a real test
}
