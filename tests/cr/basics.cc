#include <nexus/test.hh>

#include <clean-ranges/range.hh>

#include <clean-core/vector.hh>

TEST("range basics")
{
    cc::vector<int> v = {1, 2, 3, 4, 5};
    int a[] = {3, 10, 7};

    {
        auto r = cr::range(v);
        static_assert(r.is_view);
        static_assert(!r.is_owning);
        static_assert(!r.is_readonly);
    }

    {
        auto r = cr::range(cc::vector(v));
        static_assert(!r.is_view);
        static_assert(r.is_owning);
        static_assert(!r.is_readonly);
    }

    {
        auto r = cr::range(cc::vector<int>{1, 2, 3});
        static_assert(!r.is_view);
        static_assert(r.is_owning);
        static_assert(!r.is_readonly);
    }

    {
        auto const& cv = v;
        auto r = cr::range(cv);
        static_assert(r.is_view);
        static_assert(!r.is_owning);
        static_assert(r.is_readonly);
    }

    static_assert(cr::range({5, 3, 2}).sum() == 10);

    CHECK(cr::range(v).sum() == 15);
    CHECK(cr::range(a).sum() == 20);

    cr::range(v).container()[1] += 10;
    CHECK(cr::range(v).sum() == 25);

    for (auto& i : cr::range(v))
        --i;

    CHECK(cr::range(v).sum() == 20);
}

TEST("range general call")
{
    struct foo
    {
        int x;
        int f() const { return x + 1; }
        bool is_two() const { return x == 2; }
    };

    auto f = [](foo const& f) { return f.x * f.x; };

    cc::vector<foo> v;

    v.push_back({1});
    v.push_back({2});
    v.push_back({3});

    cc::vector<foo*> pv;
    pv.push_back(&v[0]);
    pv.push_back(&v[2]);

    CHECK(cr::sum(v, f) == 1 + 4 + 9);

    CHECK(cr::sum(v, &foo::x) == 6);
    CHECK(cr::sum(v, &foo::f) == 9);

    CHECK(cr::sum(pv, &foo::x) == 4);
    CHECK(cr::sum(pv, &foo::f) == 6);

    CHECK(cr::single(v, &foo::is_two).x == 2);
    cr::single(v, &foo::is_two).x = 10;
    CHECK(cr::sum(v, &foo::x) == 1 + 10 + 3);
}
