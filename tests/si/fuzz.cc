#include <nexus/fuzz_test.hh>

#include <structured-interface/element_tree.hh>
#include <structured-interface/gui.hh>
#include <structured-interface/recorded_ui.hh>
#include <structured-interface/si.hh>

namespace
{
void build_random_ui(tg::rng& rng, int depth)
{
    if (depth < 0)
        return;

    si::text("test");
    si::button("test");
}
}

FUZZ_TEST("si smoke test")(tg::rng& rng)
{
    si::gui ui;
    auto r = ui.record([&] { build_random_ui(rng, 4); });
    auto t = si::element_tree::from_record(r);
    CHECK(!t.roots().empty());
}
