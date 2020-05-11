#include <nexus/test.hh>

#include <fstream>

#include <rich-log/log.hh>

#include <structured-interface/detail/debug.hh>
#include <structured-interface/element_tree.hh>
#include <structured-interface/gui.hh>
#include <structured-interface/process/html.hh>
#include <structured-interface/si.hh>

TEST("si basics", disabled)
{
    // idea for now:
    // - recording phase
    //   - builds hierarchical, logical UI
    //   - query functions return last layout
    // - layout and styling phase
    //   - computes positions, sizes
    //   - builds render lists
    //   - associates layout data with each element ID

    si::gui ui;

    // creating si:: ui elements inside the lambda records them in 'r'
    auto r = ui.record([&] {
        // without a window there is an implicit container around everything
        si::button("press");

        si::text("test");
    });

    // std::ofstream("/tmp/ui.html") << si::process_html(ui, r).c_str();

    si::debug::print(r);
    si::debug::print(si::element_tree::from_record(r));

    CHECK(ui.has("press"));

    // internal structure concept
    // - NO special UI elements, everything is modular
    // - functions and ui element objects are just convenient wrappers
    // - recording just builds a tree structure, annotates properties, queries layout/input
    // end of recording:
    // - splice result of recording with persistent state in gui
    // - spliced properties are either overwritten or animated (on a per property basis)
    // - layout is property
    // - elements without previous state or previous state without new elements is detected (fade-in or fade-out)

    // see https://hacks.mozilla.org/2017/10/the-whole-web-at-maximum-fps-how-webrender-gets-rid-of-jank/
    // rendering quirks:
    // - try to discard things that are not visible
    // - schedule and reuse render targets
    // - culling / masking passes?
    // - glyph atlases
    // - reuse shadow stuff?
    // - effects: shadows, outlines, glass

    // UI testing
    // - has / get / find
    // - is visible / not visible
    // - is enabled / disabled
    // - simulate clicks / events
    // - is reachable / scroll to / bring into view
}
