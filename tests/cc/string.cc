#include <nexus/monte_carlo_test.hh>

#include <string>

#include <clean-core/string.hh>

MONTE_CARLO_TEST("cc::string mct")
{
    auto const make_char = [](tg::rng& rng) { return uniform(rng, 'A', 'z'); };

    addOp("gen char", make_char);

    auto const addType = [&](auto obj, bool is_cc) {
        using string_t = decltype(obj);

        addOp("default ctor", [] { return string_t(); });
        addOp("copy ctor", [](string_t const& s) { return string_t(s); });
        addOp("move ctor", [](string_t const& s) { return cc::move(string_t(s)); });
        addOp("copy assignment", [](string_t& a, string_t const& b) { a = b; });
        addOp("move assignment", [](string_t& a, string_t const& b) { a = string_t(b); });

        addOp("randomize", [&](tg::rng& rng, string_t& s) {
            auto cnt = uniform(rng, 0, 30);
            s.resize(cnt);
            for (auto i = 0; i < cnt; ++i)
                s[i] = make_char(rng);
            return s;
        });

        addOp("reserve", [](tg::rng& rng, string_t& s) { s.reserve(uniform(rng, 0, 30)); });
        addOp("resize", [](tg::rng& rng, string_t& s) { s.resize(uniform(rng, 0, 30)); });
        addOp("resize + char", [](tg::rng& rng, string_t& s, char c) { s.resize(uniform(rng, 0, 30), c); });

        addOp("random replace", [&](tg::rng& rng, string_t& s) { random_choice(rng, s) = make_char(rng); }).when([](tg::rng&, string_t const& s) {
            return s.size() > 0;
        });

        addOp("push_back", [](string_t& s, char c) { s.push_back(c); });

        addOp("op[]", [](tg::rng& rng, string_t const& s) { return random_choice(rng, s); }).when([](tg::rng&, string_t const& s) {
            return s.size() > 0;
        });
        addOp("data[]", [](tg::rng& rng, string_t const& s) { return s.data()[uniform(rng, 0, int(s.size()) - 1)]; }).when([](tg::rng&, string_t const& s) {
            return s.size() > 0;
        });

        addOp("fill", [](string_t& s, char v) {
            for (auto& c : s)
                c = v;
        });

        addOp("shrink_to_fit", &string_t::shrink_to_fit);
        addOp("clear", &string_t::clear);

        addOp("size", &string_t::size);
        addOp("front", [](string_t const& s) { return s.front(); }).when_not(&string_t::empty);
        addOp("back", [](string_t const& s) { return s.back(); }).when_not(&string_t::empty);

        addOp("+= char", [](string_t& s, char c) { s += c; });
        addOp("+= string", [](string_t& s, string_t const& rhs) { s += rhs; });
        addOp("+= lit", [](string_t& s) { s += "hello"; });

        addOp("s + s", [](string_t const& a, string_t const& b) { return a + b; });
        addOp("s + c", [](string_t const& a, char b) { return a + b; });
        addOp("s + lit", [](string_t const& a) { return a + "test"; });

        if (is_cc)
            addInvariant("cap", [](string_t const& s) { REQUIRE(s.capacity() >= 15); });
    };

    addType(std::string(), false);
    addType(cc::string(), true);

    testEquivalence<std::string, cc::string>();
}
