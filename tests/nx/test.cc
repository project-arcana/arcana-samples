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

TEST("fixed seed", seed(0xDEADBEEF))
{
    // fixed
    CHECK(nx::get_seed() == 0xDEADBEEF);
}

TEST("disabled test", disabled)
{
    volatile int i = 0;
    *reinterpret_cast<int*>(i) = 1; // guaranteed SEGFAULT
}
