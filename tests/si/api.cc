#include <nexus/test.hh>

#include <clean-core/vector.hh>

#include <structured-interface/si.hh>

namespace
{
struct foo
{
    int a = 0;
    float b = 1;
    bool c = true;
};
template <class I>
void introspect(I&& i, foo& v)
{
    i(v.a, "a");
    i(v.b, "b");
    i(v.c, "c");
}
}

TEST("si api", disabled)
{
    // see https://docs.microsoft.com/en-us/dotnet/framework/wpf/controls/control-library
    // see https://docs.microsoft.com/en-us/dotnet/framework/wpf/controls/controls-by-category
    // see https://getbootstrap.com/docs/4.4/components
    // see https://bootswatch.com/journal/

    float my_float = 1;
    int my_int = 1;
    bool my_bool = false;
    tg::color3 my_color;
    tg::pos3 my_pos;
    cc::string my_string;
    foo my_foo;

    cc::vector<float> some_floats = {1, 2, 3, 4};

    if (auto w = si::window("Structured Interfaces"))
    {
        // basics
        si::text("hello world");
        si::text("int val: {}", my_int);

        si::slider("some float", my_float, 0.0f, 1.0f);
        si::slider("some int", my_int, -10, 10);
        si::input("some int", my_int);
        si::input("some color", my_color);
        si::input("some string", my_string);
        si::input("some foo", my_foo); // uses introspect

        if (si::button("reset"))
        {
            my_float = 1;
            my_int = 1;
        }

        si::checkbox("some checkable bool", my_bool);
        si::toggle("some toggleable bool", my_bool);

        if (si::radio_button("int = 2", my_int == 2))
            my_int = 2;
        si::radio_button("int = 3", my_int, 3);
        si::radio_button("red", my_color, tg::color3::red);
        si::radio_button("blue", my_color, tg::color3::blue);

        si::dropdown("floats", my_float, some_floats);
        si::listbox("floats", my_float, some_floats);
        si::combobox("floats", my_float, some_floats);

        // progress
        // TODO: progress bar
        // TODO: spinner + growing spinner

        // buttons
        // TODO: icons
        // TODO: button groups

        // 3D
        if (si::gizmo(my_pos))
        {
            // TODO: update something
        }

        // grid
        if (auto g = si::grid())
        {
            // TODO: allow per row, per col, individual cells
            si::text("in container");
        }
        if (auto r = si::row()) // single row
        {
        }

        // trees
        if (auto t = si::tree_node("level A"))
        {
            if (auto t = si::tree_node("level A.1"))
            {
                si::text("A.1");
            }

            if (auto t = si::tree_node("level A.2"))
            {
                si::text("A.2");
            }
        }

        // custom lists
#if 0
        if (auto l = si::listbox("floats"))
        {
            si::item();
        }
        if (auto l = si::listbox("floats"))
        {
            si::item();
        }
        if (auto c = si::combobox("floats"))
        {
        }
#endif

        // tooltips & popovers
        si::text("hover over me").tooltip("I'm a tooltip!");
        si::text("hover over me").tooltip([&] { si::text("complex tooltips possible."); });
        {
            auto t = si::text("advanced tooltip");
            if (auto tt = si::tooltip(si::placement::centered_above()))
            {
                si::text("manual tooltips");
                si::text("just like any element really");
            }
        }
        // TODO: popover

        // images
        // TODO: si::image

        // modals

        // dialogs
        // TODO: file dialog

        // menu

        // status bar

        // collapsibles

        // tabs
        if (auto t = si::tabs())
        {
            if (auto t = si::tab("tab A"))
            {
            }
            if (auto t = si::tab("tab B"))
            {
            }
        }

        // canvas
        if (auto c = si::canvas())
        {
            // TODO: draw commands, vector api, something
        }

        // tables
        // TODO: freezing, reordering, hiding, resizing, sorting, style

        // layouting

        // animation

        // plotting

        // cached computation

        // cards

        // carousel

        // custom drawing

        // docking

        // themes and style

        // pagination

        // events
        // TODO: click, hover
        // TODO: disabled
        // TODO: validation
        // TODO: drag'n'drop

        // render minimap

        // custom controls

        // transparency
    }
}
