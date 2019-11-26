#include <nexus/monte_carlo_test.hh>

#include <sstream>
#include <vector>

#include <clean-core/vector.hh>

#include <clean-core/unique_ptr.hh>

namespace
{
struct api_struct
{
    int v;

    void inc() { v++; }

    void test_invariant() { CHECK(v >= 4); }

    bool can_dec() const { return v > 4; }
    void dec() { v--; }
};

int api_gen() { return 4; }
int api_add(int a, int b) { return a + b; }
api_struct api_gen_s() { return {6}; }
api_struct api_construct(int i) { return {i}; }
}

MONTE_CARLO_TEST("mct basic")
{
    addOp("gen", [](tg::rng& rng) { return uniform(rng, -10, 10) * 2; });
    addOp("add", [](int a, int b) { return a + b; });
    addOp("sub", [](int a, int b) { return a - b; });
    addOp("tri", [](int a, int b, int c) { return a * b - c; });
    addOp("inc 2", [](int& a) { a += 2; });
    addOp("div", [](int a) { return a / 2; }).when([](int a) { return a % 4 == 0; });

    addInvariant("mod 2", [](int i) { CHECK(i % 2 == 0); });
}

MONTE_CARLO_TEST("mct api")
{
    addOp("gen", api_gen);
    addOp("gen s", &api_gen_s);
    addOp("add", api_add);
    addOp("construct", api_construct);
    addOp("get v", &api_struct::v);
    addOp("inc", &api_struct::inc);
    addOp("dec", &api_struct::dec).when(&api_struct::can_dec);

    addInvariant(">= 4", &api_struct::test_invariant);
    addInvariant(">= 4", [](int i) { return i >= 4; });
}

MONTE_CARLO_TEST("mct precondition")
{
    addOp("gen", [](tg::rng& rng) { return uniform(rng, 0, 10); });
    addOp("add", [](int i) { return i + 3; });
    addOp("sub", [](int i) { return i - 5; }).when([](int i) { return i >= 5; });

    addInvariant("pos", [](int a) { CHECK(a >= 0); });
}

MONTE_CARLO_TEST("mct equivalence")
{
    addOp("gen int", [](tg::rng& rng) { return uniform(rng, -10, 10); });

    auto add_ops = [&](auto container) {
        using container_t = decltype(container);

        addOp("def ctor", [] { return container_t(); });
        addOp("clear", &container_t::clear);
        addOp("push_back", [](container_t& c, int v) { c.push_back(v); });
        addOp("pop_back", &container_t::pop_back).when_not(&container_t::empty);

        setPrinter<container_t>([](container_t const& c) -> cc::string {
            std::stringstream ss;
            ss << "[";
            for (auto i = 0u; i < c.size(); ++i)
            {
                if (i > 0)
                    ss << ", ";
                ss << c[i];
            }
            ss << "]";
            return ss.str().c_str();
        });
    };

    add_ops(std::vector<int>());
    add_ops(cc::vector<int>());

    testEquivalence([](std::vector<int> const& a, cc::vector<int> const& b) {
        CHECK(a.size() == b.size());
        auto s = a.size();
        for (size_t i = 0; i < s; ++i)
            CHECK(a[i] == b[i]);
    });
}

MONTE_CARLO_TEST("mct replay", reproduce("-000302004-10"), disabled) // for testing purposes
{
    addValue("4", 4);

    addOp("add", [](int a, int b) { return a + b; });
    addOp("inc", [](int& a) { a++; });
    addOp("sub", [](int a, int b) { return a - b; });
    addOp("sub3", [](int& a) { a -= 3; });

    addInvariant(">= 0", [](int a) { CHECK(a >= 0); });
}

// TODO: proper move-only impl
#if 0
MONTE_CARLO_TEST("mct move-only")
{
    addOp("gen", [] { return cc::make_unique<int>(0); });
    addOp("add A", [](cc::unique_ptr<int> i) {
        *i += 1;
        return cc::move(i);
    });
    addOp("add B", [](cc::unique_ptr<int>& i) { *i += 1; });
    addInvariant("pos", [](cc::unique_ptr<int> const& i) { CHECK(*i >= 0); });
}
#endif
