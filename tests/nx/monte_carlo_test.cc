#include <nexus/monte_carlo_test.hh>

MONTE_CARLO_TEST("nexus mct")
{
    addOp("gen", [](tg::rng& rng) { return uniform(rng, -10, 10) * 2; });
    addOp("add", [](int a, int b) { return a + b; });
    addOp("sub", [](int a, int b) { return a - b; });
    addOp("tri", [](int a, int b, int c) { return a * b - c; });
    addOp("inc 2", [](int& a) { a += 2; });
    addInvariant("mod 2", [](int i) { CHECK(i % 2 == 0); });

    // testSubstitutability([](int a, long long b) { CHECK(a == b); });
}
