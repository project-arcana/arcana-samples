#include <nexus/test.hh>

#include <clean-core/function_ref.hh>

namespace
{
int test_fun(int a, int b) { return a + b; }
}

TEST("cc::function_ref")
{
    cc::function_ref<int(int, int)> f = test_fun;

    CHECK(f(1, 2) == 3);

    int x = 7;
    auto l = [&](int a, int b) { return a + b + x; };

    f = l;
    CHECK(f(1, 2) == 10);

    x = 5;
    CHECK(f(1, 2) == 8);
}
