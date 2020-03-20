#include <nexus/test.hh>

#include <clean-core/array.hh>
#include <clean-core/vector.hh>

#include <clean-ranges/algorithms.hh>
#include <clean-ranges/range.hh>

TEST("cr copy guarantees")
{
    struct immovable_range
    {
        cc::array<int> a = {1, 2, 3, 4, 5};
        auto begin() const { return a.begin(); }
        auto end() const { return a.end(); }

        immovable_range() = default;
        immovable_range(immovable_range const&) = delete;
        immovable_range(immovable_range&&) = delete;
        immovable_range& operator=(immovable_range const&) = delete;
        immovable_range& operator=(immovable_range&&) = delete;
    };
    immovable_range ir;

    struct immov_plus1
    {
        immov_plus1() = default;
        immov_plus1(immov_plus1 const&) = delete;
        immov_plus1(immov_plus1&&) = delete;
        immov_plus1& operator=(immov_plus1 const&) = delete;
        immov_plus1& operator=(immov_plus1&&) = delete;

        int operator()(int x) const { return x + 1; }
    };
    immov_plus1 plus_one;

    struct immov_is_odd
    {
        immov_is_odd() = default;
        immov_is_odd(immov_is_odd const&) = delete;
        immov_is_odd(immov_is_odd&&) = delete;
        immov_is_odd& operator=(immov_is_odd const&) = delete;
        immov_is_odd& operator=(immov_is_odd&&) = delete;

        bool operator()(int x) const { return x % 2 != 0; }
    };
    immov_is_odd is_odd;

    CHECK(cr::range(ir) == cc::vector{1, 2, 3, 4, 5});
    CHECK(cr::range(ir).sum() == 15);
    CHECK(cr::range(ir).sum(plus_one) == 20);
    CHECK(cr::range(ir).map(plus_one).sum() == 20);
    CHECK(cr::range(ir).map(plus_one).where(is_odd).sum() == 3 + 5);
    CHECK(cr::range(ir).map(plus_one).where_not(is_odd).sum() == 2 + 4 + 6);
    CHECK(cr::range(ir).map_where(plus_one, is_odd).sum() == 2 + 4 + 6);

    // TODO: zip, concat
}