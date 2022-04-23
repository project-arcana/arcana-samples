#include <nexus/test.hh>

#include <clean-core/set.hh>

#include <texture-processor/image.hh>

#include <typed-geometry/feature/basic.hh>

#include <rich-log/log.hh>

TEST("iteration")
{
    cc::set<tg::ipos2, tg::hash> visited;

    auto img = tp::image2<float>::filled({4, 3}, 0.f);
    for (auto p : img.positions())
    {
        CHECK(!visited.contains(p));
        CHECK(img.contains(p));
        visited.add(p);
    }

    CHECK(visited.size() == img.extent().pixel_count());

    for (auto&& [p, v] : img)
        v = p.x + p.y;
    CHECK(img(3, 2) == 3 + 2);
}
