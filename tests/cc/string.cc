#include <nexus/test.hh>

#include <string>

#include <clean-core/string.hh>

#include <iostream>

#include <typed-geometry/feature/random.hh>

namespace
{
template <class string_t>
struct string_tester
{
    using T = std::decay_t<decltype(std::declval<string_t>()[0])>;

    tg::rng rng;
    string_t v;

    auto make_obj() { return uniform(rng, 'A', 'z'); }
    string_t make_str()
    {
        string_t v;
        auto s = uniform(rng, 0, 40);
        for (auto i = 0; i < s; ++i)
            v.push_back(make_obj());
        return v;
    }

    void step()
    {
        auto nr = uniform(rng, 0, 13);
        std::cout << nr << std::endl;
        switch (nr)
        {
        case 0:
            v.clear();
            break;
        case 1:
            if (!v.empty())
                v.pop_back();
            break;
        case 2:
            if (v.size() < 20)
                v.push_back(make_obj());
            break;
        case 3:
            break;
        case 4:
            v = make_str(); // move
            break;
        case 5:
        {
            auto v2 = make_str();
            v = v2; // copy
        }
        break;
        case 6:
            v.resize(size_t(uniform(rng, 0, 5)));
            break;
        case 7:
            v.resize(size_t(uniform(rng, 0, 5)), make_obj());
            break;
        case 8:
            v.reserve(size_t(uniform(rng, 0, 10)));
            break;
        case 9:
            v.shrink_to_fit();
            break;
        case 10:
            break;
        case 11:
            v = string_t(make_str()); // move ctor
            break;
        case 12:
        {
            auto v2 = make_str();
            v = string_t(v2); // copy ctor
        }
        break;
        case 13:
            if (!v.empty())
                v[uniform(rng, size_t(0), v.size() - 1)] = make_obj();
            break;
        default:
            break;
        }
    }

    template <class other_string_t>
    void check_equal(other_string_t const& rhs) const
    {
        auto const& v0 = v;
        auto const& v1 = rhs.v;

        REQUIRE(v0.size() == v1.size());
        CHECK(v0.empty() == v1.empty());
        std::cout << v0.size() << " vs " << v1.size() << std::endl;
        for (auto i = 0u; i < v0.size(); ++i)
            CHECK(v0[i] == v1[i]);
        CHECK(v0 == v0);
        CHECK(v1 == v1);
    }
};
}

TEST("string basics")
{
    tg::rng rng;

    auto const test = [&](auto&& v0, auto&& v1) {
        //
        auto s = rng();
        v0.rng.seed(s);
        v1.rng.seed(s);

        for (auto i = 0; i < 100; ++i)
        {
            v1.check_equal(v0);

            v0.step();
            v1.step();

            v0.check_equal(v1);
        }
    };

    // test new string and string-like types
    for (auto i = 0; i < 10; ++i)
    {
        test(string_tester<std::string>(), string_tester<cc::string>());
    }
}
