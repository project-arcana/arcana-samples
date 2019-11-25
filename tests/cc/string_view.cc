#include <nexus/test.hh>

#include <clean-core/string_view.hh>

TEST("cc::string_view")
{
    cc::string_view s;
    CHECK(s.empty());

    s = "hello";
    CHECK(s[0] == 'h');
    CHECK(s.size() == 5);
    CHECK(s[4] == 'o');
    CHECK(s == "hello");
}
