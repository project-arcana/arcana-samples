#include <nexus/test.hh>

#include <resource-system/System.hh>
#include <resource-system/res.hh>

TEST("res explicit resource")
{
    auto h = res::create(7);

    static_assert(std::is_same_v<decltype(h), res::handle<int>>);
    CHECK(h.is_valid());
    CHECK(h.is_loaded());
    CHECK(h.get() == 7);
    CHECK(h.try_get() != nullptr);
    CHECK(*h.try_get() == 7);
}

TEST("res simple define")
{
    auto h = res::define([](float a, float b) { return a + b; }, 1, 2.f);

    static_assert(std::is_same_v<decltype(h), res::handle<float>>);
    CHECK(h.is_valid());
    CHECK(!h.is_loaded());

    res::system().process_all();

    CHECK(!h.is_loaded()); // never requested
    h.request();
    CHECK(h.try_get() == nullptr); // still not loaded

    res::system().process_all();

    CHECK(h.is_loaded());
    CHECK(h.get() == 3);
}

TEST("res dependent define")
{
    auto add = [](float a, float b) { return a + b; };
    auto c3 = res::create(3.0f);
    auto h0 = res::define(add, 1.0f, 2.f);
    auto h1 = res::define(add, h0, 5.0f);
    auto h2 = res::define(add, h0, h1);
    auto h3 = res::define(add, h2, h2);
    auto h4 = res::define(add, c3, h3);

    CHECK(h4.try_get() == nullptr); // not loaded but now requested

    res::system().process_all();

    CHECK(c3.get() == 3);
    CHECK(h0.get() == 3);
    CHECK(h1.get() == 8);
    CHECK(h2.get() == 11);
    CHECK(h3.get() == 22);
    CHECK(h4.get() == 25);
}
