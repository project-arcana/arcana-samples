#include <nexus/test.hh>

#include <structured-interface/merger/StyleSheet.hh>

TEST("stylesheets")
{
    using computed_style = si::StyleSheet::computed_style;

    si::StyleSheet ss;
    CHECK(ss.get_style_rule_count() == 0);

    auto string_to_type = [](cc::string_view s) -> si::element_type {
        for (auto i = 0; i < 128; ++i)
            if (to_string(si::element_type(i)) == s)
                return si::element_type(i);
        CC_UNREACHABLE("unknown type");
    };

    auto style_of = [&](cc::string_view elems) {
        cc::vector<si::StyleSheet::style_key> keys;
        for (auto ss : elems.split())
        {
            auto k = cc::bit_cast<si::StyleSheet::style_key>(si::StyleSheet::style_key_int_t(0));
            auto is_first = true;
            for (auto s : ss.split(':'))
            {
                if (is_first) // element
                {
                    k.type = string_to_type(s);
                    is_first = false;
                }
                else // modifier
                {
                    if (s == "hover")
                        k.is_hovered = true;
                    else if (s == "press")
                        k.is_pressed = true;
                    else
                        CC_UNREACHABLE("not supported");
                }
            }
            keys.push_back(k);
        }
        auto curr = keys.back();
        keys.pop_back();
        return ss.compute_style(curr, keys);
    };

    ss.add_rule("*", [](computed_style& s) { s.margin.left = 17; });
    ss.add_rule(":hover", [](computed_style& s) { s.margin.left = 32; });
    ss.add_rule(":press", [](computed_style& s) { s.margin.left = 40; });
    ss.add_rule("text", [](computed_style& s) { s.margin.left = 16; });

    CHECK(style_of("box").margin.left == 17);
    CHECK(style_of("box:hover").margin.left == 32);
    CHECK(style_of("box:press").margin.left == 40);
    CHECK(style_of("box box").margin.left == 17);
    CHECK(style_of("box box:hover").margin.left == 32);
    CHECK(style_of("box box:press").margin.left == 40);
    CHECK(style_of("box:hover box").margin.left == 17);
    CHECK(style_of("box:hover text").margin.left == 16);

    ss.add_rule("text box", [](computed_style& s) { s.margin.left = 100; });
    ss.add_rule("box text", [](computed_style& s) { s.margin.left = 200; });

    CHECK(style_of("box").margin.left == 17);
    CHECK(style_of("text").margin.left == 16);
    CHECK(style_of("text box").margin.left == 100);
    CHECK(style_of("text box box").margin.left == 100);
    CHECK(style_of("text text box").margin.left == 100);
    CHECK(style_of("text box text box").margin.left == 100);
    CHECK(style_of("text box:hover text box").margin.left == 100);
    CHECK(style_of("box text").margin.left == 200);
    CHECK(style_of("box box text").margin.left == 200);
    CHECK(style_of("box box text text").margin.left == 200);
    CHECK(style_of("text box text text").margin.left == 200);
    CHECK(style_of("text box text:press text").margin.left == 200);
    CHECK(style_of("text box text:press text:hover").margin.left == 200);

    ss.add_rule("window:hover box:press text", [](computed_style& s) { s.margin.left = 123; });
    CHECK(style_of("text").margin.left == 16);
    CHECK(style_of("box text").margin.left == 200);
    CHECK(style_of("window box text").margin.left == 200);
    CHECK(style_of("window:hover box text").margin.left == 200);
    CHECK(style_of("window box:press text").margin.left == 200);
    CHECK(style_of("window:hover box:press text").margin.left == 123);
}
