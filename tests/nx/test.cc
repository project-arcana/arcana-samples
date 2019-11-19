#include <nexus/test.hh>

TEST("test")
{
    CHECK(1 + 1 == 2);
    REQUIRE(1 << 3 == 8);
}
