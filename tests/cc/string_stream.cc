#include <nexus/test.hh>

#include <clean-core/string_stream.hh>

TEST("cc::string_stream")
{
    cc::string_stream ss;
    CHECK(ss.empty());
    ss << "";
    CHECK(ss.empty());
    ss << "foo";
    CHECK(ss.size() == 3);
    CHECK(ss.to_string() == "foo");
    ss << "bar";
    CHECK(ss.size() == 6);
    CHECK(ss.to_string() == "foobar");
    ss.clear();
    CHECK(ss.empty());

    cc::string_stream ss2;
    ss2 = ss;
    CHECK(ss.empty());
    CHECK(ss2.empty());
    ss << "foo";
    ss2 = cc::move(ss);
    CHECK(ss.empty());
    CHECK(ss2.to_string() == "foo");

    ss << "foo"
       << "bar";
    CHECK(ss.to_string() == "foobar");
}
