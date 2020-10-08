#include <nexus/test.hh>

#include <reflector/introspect.hh>
#include <reflector/macros.hh>
#include <reflector/members.hh>

namespace
{
struct foo
{
    int a;
    bool bc;
    double def;
};

REFL_MAKE_INTROSPECTABLE(foo, a, bc, def);

struct bar
{
    int x;
    float y;
};

REFL_INTROSPECT(bar)
{
    REFL_FIELD(x);
    inspect(v.y, "y");
}

struct baz
{
    bool b;
    char c;
    int i, j;
};

REFL_INTROSPECT(baz) { REFL_FIELDS(b, c, i, j); }
}

TEST("macro based introspect decl")
{
    CHECK(rf::get_member_count<foo>() == 3);
    CHECK(rf::get_member_infos<foo>()[0].name == "a");
    CHECK(rf::get_member_infos<foo>()[1].name == "bc");
    CHECK(rf::get_member_infos<foo>()[2].name == "def");

    CHECK(rf::get_member_count<bar>() == 2);
    CHECK(rf::get_member_infos<bar>()[0].name == "x");
    CHECK(rf::get_member_infos<bar>()[1].name == "y");

    CHECK(rf::get_member_count<baz>() == 4);
    CHECK(rf::get_member_infos<baz>()[0].name == "b");
    CHECK(rf::get_member_infos<baz>()[1].name == "c");
    CHECK(rf::get_member_infos<baz>()[2].name == "i");
    CHECK(rf::get_member_infos<baz>()[3].name == "j");
}
