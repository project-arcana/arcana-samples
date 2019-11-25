#include <nexus/test.hh>

TEST("test")
{
    CHECK(1 + 1 == 2);
    REQUIRE(1 << 3 == 8);
}

TEST("should fail test", should_fail)
{
    CHECK(1 + 1 == 3);
    CHECK(1 + 1 != 2);
}
