#include <nexus/test.hh>

#include <vector>

#include <clean-core/capped_vector.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/feature/random.hh>

#include "special_types.hh"

namespace
{
template <class T, class = void>
struct has_reserve_t : std::false_type
{
};
template <class T>
struct has_reserve_t<T, std::void_t<decltype(T::reserve)>> : std::true_type
{
};

template <class vector_t>
struct vector_tester
{
    using T = std::decay_t<decltype(std::declval<vector_t>()[0])>;

    static constexpr bool has_reserve = has_reserve_t<T>::value;
    static constexpr bool has_default_ctor = std::is_default_constructible_v<T>;
    static constexpr bool is_copyable = std::is_copy_assignable_v<T>;

    tg::rng rng;
    vector_t v;

    auto make_obj()
    {
        if constexpr (std::is_same_v<T, int>)
            return uniform(rng, -10, 10);
        else if constexpr (std::is_same_v<T, no_default_type>)
            return no_default_type(uniform(rng, -10, 10));
        else if constexpr (std::is_same_v<T, move_only_type>)
            return move_only_type(uniform(rng, -10, 10));
        else
            static_assert(cc::always_false<T>, "not implemented");
    }
    vector_t make_vec()
    {
        vector_t v;
        auto s = uniform(rng, 0, 4);
        for (auto i = 0; i < s; ++i)
            v.push_back(make_obj());
        return v;
    }

    void step()
    {
        switch (uniform(rng, 0, 13))
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
            if (v.size() < 20)
                v.emplace_back(make_obj());
            break;
        case 4:
            v = make_vec(); // move
            break;
        case 5:
            if constexpr (is_copyable)
            {
                auto v2 = make_vec();
                v = v2; // copy
            }
            break;
        case 6:
            if constexpr (has_default_ctor && is_copyable) // not defined for default ctor
                v.resize(size_t(uniform(rng, 0, 5)));
            break;
        case 7:
            if constexpr (is_copyable)
                v.resize(size_t(uniform(rng, 0, 5)), make_obj());
            break;
        case 8:
            if constexpr (has_reserve)
                v.reserve(size_t(uniform(rng, 0, 10)));
            break;
        case 9:
            if constexpr (has_reserve)
                v.shrink_to_fit();
            break;
        case 10:
            if constexpr (has_default_ctor) // not defined for default ctor
                if (v.size() < 20)
                    v.emplace_back() = make_obj();
            break;
        case 11:
            if constexpr (is_copyable)
                v = vector_t(make_vec()); // move ctor
            break;
        case 12:
            if constexpr (is_copyable)
            {
                auto v2 = make_vec();
                v = vector_t(v2); // copy ctor
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

    template <class other_vector_t>
    void check_equal(other_vector_t const& rhs) const
    {
        auto const& v0 = v;
        auto const& v1 = rhs.v;

        CHECK(v0.size() == v1.size());
        CHECK(v0.empty() == v1.empty());
        for (auto i = 0u; i < v0.size(); ++i)
            CHECK(v0[i] == v1[i]);
        CHECK(v0 == v0);
        CHECK(v1 == v1);
    }
};
}

TEST("vector basics")
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

    auto const type_test = [&](auto t) {
        using T = decltype(t);
        // test new vector and vector-like types
        for (auto i = 0; i < 10; ++i)
        {
            test(vector_tester<std::vector<T>>(), vector_tester<cc::vector<T>>());
            test(vector_tester<std::vector<T>>(), vector_tester<cc::capped_vector<T, 20>>());
        }
    };

    type_test(int());
    type_test(no_default_type(0));
    type_test(move_only_type(0));

    // TODO: count constructions!
}
