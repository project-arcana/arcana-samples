#include <nexus/test.hh>

#include <clean-core/vector.hh>

#include <babel-serializer/data/json.hh>

namespace
{
struct foo
{
    int x = 2;
    bool b = false;
};
template <class I>
constexpr void introspect(I&& i, foo& v)
{
    i(v.x, "x");
    i(v.b, "b");
}
}

TEST("json basics")
{
    // primitives
    for (auto indent : {-1, 0, 2})
    {
        babel::json::write_config cfg;
        cfg.indent = indent;

        CHECK(babel::json::to_string(nullptr, cfg) == "null");
        CHECK(babel::json::to_string(true, cfg) == "true");
        CHECK(babel::json::to_string(false, cfg) == "false");
        CHECK(babel::json::to_string(0, cfg) == "0");
        CHECK(babel::json::to_string(1, cfg) == "1");
        CHECK(babel::json::to_string(-12, cfg) == "-12");
        CHECK(babel::json::to_string(0.5, cfg) == "0.5");
        CHECK(babel::json::to_string(-0.25f, cfg) == "-0.25");
        CHECK(babel::json::to_string(std::byte(100), cfg) == "100");
        CHECK(babel::json::to_string('a', cfg) == "\"a\"");
        CHECK(babel::json::to_string("hello", cfg) == "\"hello\"");
    }

    // composites
    {
        int v[] = {1, 2, 3};
        CHECK(babel::json::to_string(v) == "[1,2,3]");
    }
    {
        cc::vector<int> v = {1, 2, 3};
        CHECK(babel::json::to_string(v) == "[1,2,3]");
    }
    {
        cc::vector<cc::vector<int>> v = {{}, {1}, {2, 3}};
        CHECK(babel::json::to_string(v) == "[[],[1],[2,3]]");
    }
    {
        CHECK(babel::json::to_string(foo{}) == "{\"x\":2,\"b\":false}");
    }

    // indent
    {
        int v[] = {1, 2, 3};
        CHECK(babel::json::to_string(v, babel::json::write_config{2})
              == "[\n"
                 "  1,\n"
                 "  2,\n"
                 "  3\n"
                 "]");
        CHECK(babel::json::to_string(v, babel::json::write_config{4})
              == "[\n"
                 "    1,\n"
                 "    2,\n"
                 "    3\n"
                 "]");
    }
    {
        cc::vector<int> v;
        CHECK(babel::json::to_string(v, babel::json::write_config{2})
              == "[\n"
                 "]");
    }
    {
        cc::vector<cc::vector<int>> v = {{}, {1}};
        CHECK(babel::json::to_string(v, babel::json::write_config{2})
              == "[\n"
                 "  [\n"
                 "  ],\n"
                 "  [\n"
                 "    1\n"
                 "  ]\n"
                 "]");
    }
    {
        CHECK(babel::json::to_string(foo{}, babel::json::write_config{2})
              == "{\n"
                 "  \"x\": 2,\n"
                 "  \"b\": false\n"
                 "}");
    }
    {
        cc::vector<foo> v = {{2, true}, {3, false}};
        CHECK(babel::json::to_string(v, babel::json::write_config{2})
              == "[\n"
                 "  {\n"
                 "    \"x\": 2,\n"
                 "    \"b\": true\n"
                 "  },\n"
                 "  {\n"
                 "    \"x\": 3,\n"
                 "    \"b\": false\n"
                 "  }\n"
                 "]");
    }
}
