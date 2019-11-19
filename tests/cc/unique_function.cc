#include <nexus/test.hh>

#include <clean-core/unique_function.hh>

TEST("cc::unique_function")
{
    cc::unique_function<int(int)> f;

    CHECK(!f);

    f = [](int i) { return i * 2; };

    CHECK(f(7) == 14);

    auto f2 = cc::move(f);

    CHECK(!f);
    CHECK(f2(8) == 16);
}
