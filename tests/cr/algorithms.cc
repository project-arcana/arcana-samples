#include <nexus/test.hh>

#include <clean-core/vector.hh>

#include <clean-ranges/algorithms.hh>

namespace
{
int plus_one(int x) { return x + 1; }

int prod(int a, int b) { return a * b; }

bool is_sqr_equal_sum(int x) { return x * x == x + x; }

bool is_odd(int x) { return x % 2 != 0; }
bool is_positive(int x) { return x > 0; }
bool is_negative(int x) { return x < 0; }

int mod3(int x) { return x % 3; }
}

TEST("cr algorithms")
{
    std::initializer_list<int> empty = {};
    int v[] = {4, 3, 1, 2};

    auto const times_two = [](int x) { return x * 2; };

    CHECK(cr::sum(v) == 10);
    CHECK(cr::sum(v, plus_one) == 14);

    CHECK(cr::reduce(v, prod) == 4 * 3 * 1 * 2);
    CHECK(cr::reduce(v, prod, times_two) == 4 * 3 * 1 * 2 * 2 * 2 * 2 * 2);

    CHECK(cr::single(v, is_sqr_equal_sum) == 2);

    CHECK(cr::first(v) == 4);
    CHECK(cr::first(v, is_odd) == 3);

    CHECK(cr::last(v) == 2);
    CHECK(cr::last(v, is_odd) == 1);

    CHECK(!cr::any(empty));
    CHECK(cr::any(v));
    CHECK(cr::any(v, is_odd));
    CHECK(!cr::any(v, is_negative));

    CHECK(!cr::all(v, is_odd));
    CHECK(cr::all(v, is_positive));

    CHECK(cr::count(v) == 4);
    CHECK(cr::count(v, 3) == 1);
    CHECK(cr::count(v, -1) == 0);
    CHECK(cr::count_if(v, is_odd) == 2);

    CHECK(cr::contains(v, 1));
    CHECK(!cr::contains(v, -1));

    CHECK(cr::min(v) == 1);
    CHECK(cr::min(v, times_two) == 2);

    CHECK(cr::max(v) == 4);
    CHECK(cr::max(v, times_two) == 8);

    CHECK(cr::minmax(v).min == 1);
    CHECK(cr::minmax(v).max == 4);
    CHECK(cr::minmax(v, times_two).min == 2);
    CHECK(cr::minmax(v, times_two).max == 8);

    CHECK(cr::min_by(v, mod3) == 3);
    CHECK(cr::max_by(v, mod3) == 2);
    CHECK(cr::minmax_by(v, mod3).min == 3);
    CHECK(cr::minmax_by(v, mod3).max == 2);

    CHECK(!cr::is_empty(v));
    CHECK(cr::is_non_empty(v));
    CHECK(cr::is_empty(empty));
    CHECK(!cr::is_non_empty(empty));
}

TEST("cr modifying algorithms")
{
    cc::vector<int> v;

    v = {2, 3, 4};
    cr::single(v, is_odd) = 10;
    CHECK(v == cc::vector{2, 10, 4});

    v = {2, 3, 4};
    cr::first(v) = 10;
    CHECK(v == cc::vector{10, 3, 4});

    v = {2, 3, 4};
    cr::first(v, is_odd) = 10;
    CHECK(v == cc::vector{2, 10, 4});

    v = {2, 3, 4};
    cr::min(v) = 10;
    CHECK(v == cc::vector{10, 3, 4});

    // cr::min(v, plus_one) = 3; // ERROR: expression is not assignable (this is good)

    v = {2, 3, 4};
    cr::max(v) = 10;
    CHECK(v == cc::vector{2, 3, 10});

    v = {4, 2, 3};
    {
        auto&& [vmin, vmax] = cr::minmax(v);
        vmin = 7;
        vmax = 9;
    }
    CHECK(v == cc::vector{9, 7, 3});

    v = {4, 2, 3};
    cr::min_by(v, mod3) = 10;
    CHECK(v == cc::vector{4, 2, 10});

    v = {4, 2, 3};
    cr::max_by(v, mod3) = 10;
    CHECK(v == cc::vector{4, 10, 3});

    v = {4, 2, 3};
    {
        auto&& [vmin, vmax] = cr::minmax_by(v, mod3);
        vmin = 7;
        vmax = 9;
    }
    CHECK(v == cc::vector{4, 9, 7});
}
