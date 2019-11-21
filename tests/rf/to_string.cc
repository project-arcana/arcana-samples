#include <nexus/test.hh>

#include <reflector/to_string.hh>

namespace
{
struct foo
{
};
cc::string to_string(foo const&) { return "foo"; }
}

TEST("rf::to_string basics")
{
    cc::string s = "234";

    // cc::to_string
    CHECK(rf::to_string(12345) == "12345");
    CHECK(rf::to_string(12345L) == "12345");
    CHECK(rf::to_string(12345LL) == "12345");
    CHECK(rf::to_string(12345u) == "12345");
    CHECK(rf::to_string(12345uL) == "12345");
    CHECK(rf::to_string(12345uLL) == "12345");
    CHECK(rf::to_string("123") == "123");
    CHECK(rf::to_string(s) == "234");
    CHECK(rf::to_string(true) == "true");
    CHECK(rf::to_string(false) == "false");
    CHECK(rf::to_string('z') == "z");
    CHECK(rf::to_string(nullptr) == "nullptr");
    CHECK(rf::to_string((void*)0x1234) == "0x0000000000001234");

    // custom to_string
    CHECK(rf::to_string(foo{}) == "foo");
}
