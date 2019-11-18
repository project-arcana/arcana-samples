#include <nexus/test.hh>

#include <array>
#include <iostream>
#include <thread>
#include <vector>

#include <task-dispatcher/common/math_intrin.hh>
#include <task-dispatcher/td.hh>

namespace
{
int gSink = 0;

void fun() { ++gSink; }
void argfun(int a, int b, int c) { gSink += (a + b + c); }

int retfun() { return ++gSink; }
int retargfun(int a, int b, int c)
{
    gSink += (a + b + c);
    return gSink;
}

template <class T>
bool constexpr is_int_future = std::is_same_v<td::future<int>, T>;

struct foo
{
    void met() { ++gSink; }
    void argmet(int a, int b, int c) { gSink += (a + b + c); }
    int retmet() { return ++gSink; }
    int retargmet(int a, int b, int c)
    {
        gSink += (a + b + c);
        return gSink;
    }
};
}

TEST("td API - compilation", exclusive)
{
    /// Aims to cover the entire API surface, making compilation or logic errors visible
    /// Little to no asserts

    // Launch variants
    {
        CHECK(!td::is_scheduler_alive());

        // simple
        td::launch([] { CHECK(td::is_scheduler_alive()); });

        // rvalue config
        td::launch(td::scheduler_config{}, [] { CHECK(td::is_scheduler_alive()); });

        // lvalue config
        td::scheduler_config conf;
        td::launch(conf, [] { CHECK(td::is_scheduler_alive()); });

        // nested
        td::launch([] { td::launch([] { td::launch([] { CHECK(td::is_scheduler_alive()); }); }); });

        CHECK(!td::is_scheduler_alive());
    }

    td::launch([] {
        REQUIRE(td::is_scheduler_alive());

        // submit - single
        {
            (void)0; // for clang-format

            // sync argument
            {
                td::sync s1;

                // Without arguments
                td::submit(s1, [] {});
                td::submit(s1, +[] {});
                td::submit(s1, fun);

                foo f;
                // auto s2 = td::submit_nonoverload(&foo::argmet, f, 1, 2, 3);

                // With arguments
                td::submit(s1, [](float arg) { gSink += int(arg); }, 1.f);
                td::submit(s1, argfun, 4, 5, 6);

                td::wait_for(s1);
            }

            // sync return
            {
                // Without arguments
                auto s1 = td::submit([] {});
                auto s2 = td::submit(fun);

                // With arguments
                auto s3 = td::submit([](float arg) { gSink += int(arg); }, 1.f);
                auto s4 = td::submit(argfun, 4, 5, 6);

                td::wait_for(s1, s2, s3, s4);

                // redundant wait
                td::wait_for(s1, s2, s3, s4);
                td::wait_for_unpinned(s1, s2, s3, s4);
            }
        }

        // submit - single with return
        {
            // void return
            auto s1 = td::submit([] {});
            auto s2 = td::submit(+[] {});
            auto s3 = td::submit(fun);
            auto s4 = td::submit([](auto arg) { gSink += arg; }, 5);
            auto s5 = td::submit(argfun, 5, 6, 7);

            td::wait_for(s1, s2, s3, s4, s5);

            // future return
            auto f1 = td::submit([] { return 1; });
            auto f2 = td::submit(+[] { return 1; });
            auto f3 = td::submit(retfun);
            auto f4 = td::submit(
                [](auto arg) {
                    gSink += arg;
                    return gSink;
                },
                5);
            auto f5 = td::submit(retargfun, 8, 9, 10);

            static_assert(is_int_future<decltype(f1)>);
            static_assert(is_int_future<decltype(f2)>);
            static_assert(is_int_future<decltype(f3)>);
            static_assert(is_int_future<decltype(f4)>);
            static_assert(is_int_future<decltype(f5)>);
        }

        // submit - n, each, batched
        {
            (void)0; // for clang-format

            // sync argument
            {
                td::sync s1;

                td::submit_n(s1, [](auto i) { gSink += i; }, 50);

                std::array<int, 50> values;
                td::submit_each_ref<int>(s1, [](int& val) { ++val; }, values);

                td::submit_batched(s1,
                                   [](auto begin, auto end) {
                                       for (auto i = begin; i < end; ++i)
                                           gSink += i;
                                   },
                                   500);

                td::wait_for(s1);
            }

            // sync return
            {
                auto s1 = td::submit_n([](auto i) { gSink += i; }, 50);

                std::array<int, 50> values;
                auto s2 = td::submit_each_ref<int>([](int& val) { ++val; }, values);

                auto s3 = td::submit_batched(
                    [](auto begin, auto end) {
                        for (auto i = begin; i < end; ++i)
                            gSink += i;
                    },
                    500);

                td::wait_for(s1, s2, s3);
            }
        }

        // move-only arguments and lambdas
        {
            auto u1 = std::make_unique<int>(1);
            auto u2 = std::make_unique<int>(2);
            auto u3 = std::make_unique<int>(3);
            auto u4 = std::make_unique<int>(4);

            auto l_moveonly = [u = std::move(u1)] { gSink += *u; };

            td::sync s;
            td::submit(s, std::move(l_moveonly));
            td::submit(s, [](std::unique_ptr<int> const& u) { gSink += *u; }, u2);
            // td::submit(s, [](std::unique_ptr<int> u) { gSink += *u; }, std::move(u3)); // TODO: Error!

            td::wait_for(s);
        }
    });

    CHECK(!td::is_scheduler_alive());
}


TEST("td API - consistency", exclusive)
{
    /// Basic consistency and sanity checks, WIP

    CHECK(!td::is_scheduler_alive());

    td::launch([] {
        CHECK(td::is_scheduler_alive());

        // Nesting
        td::launch([] { CHECK(td::is_scheduler_alive()); });

        CHECK(td::is_scheduler_alive());

        auto const main_thread_id = std::this_thread::get_id();

        // Staying on the main thread when using pinned wait
        auto const num_tasks = td::system::hardware_concurrency * 50;
        for (auto _ = 0; _ < 50; ++_)
        {
            auto s1 = td::submit_n([](auto) { ++gSink; }, num_tasks);

            td::wait_for(s1);
            CHECK(std::this_thread::get_id() == main_thread_id);
        }

        // Nested Multi-wait
        {
            td::sync s1, s2, s3;
            int a = 0, b = 0, c = 0;

            td::submit(s1, [&] {
                td::submit(s2, [&] {
                    CHECK(a == 0);
                    CHECK(b == 0);
                    CHECK(c == 0);
                });

                td::wait_for(s2);

                a = 1;
                b = 2;
                c = 3;
            });

            td::submit(s3, [&] {
                td::wait_for(s1);
                CHECK(a == 1);
                CHECK(b == 2);
                CHECK(c == 3);
            });

            td::wait_for(s3);

            CHECK(!s1.initialized);
            CHECK(!s2.initialized);
            CHECK(!s3.initialized);
            CHECK(std::this_thread::get_id() == main_thread_id);
        }

        // Chained dependency
        {
            auto constexpr n = 50;

            std::array<int, n> res_array;
            std::fill(res_array.begin(), res_array.end(), 0);

            std::array<td::sync, n> syncs;
            std::fill(syncs.begin(), syncs.end(), td::sync{});

            for (auto i = 0; i < n; ++i)
            {
                CHECK(res_array[i] == 0);
                if (i < n - 1)
                {
                    // TODO: Why doesn't this compile with [&res_array, i] ?
                    td::submit(syncs[i], [dat_ptr = res_array.data(), i] { dat_ptr[i] = i; });
                }

                if (i > 0)
                {
                    td::wait_for(syncs[i - 1]);
                    CHECK(res_array[i - 1] == i - 1);
                }
            }

            td::wait_for(syncs[n - 1]);
            CHECK(std::this_thread::get_id() == main_thread_id);
        }

        // Number of executions
        {
            auto constexpr n = 500;
            std::atomic_int counter = {0};
            CHECK(counter.load() == 0);
            auto s = td::submit_n([&](auto) { ++counter; }, n);
            td::wait_for(s);
            CHECK(counter.load() == n);
            CHECK(std::this_thread::get_id() == main_thread_id);
        }

        // submit_each
        {
            std::vector<int> values;
            values.resize(30, 0);

            for (auto v : values)
                CHECK(v == 0);

            auto s = td::submit_each_ref<int>([](int& v) { v = 1; }, values);

            td::wait_for(s);

            for (auto v : values)
                CHECK(v == 1);
        }
    });

    CHECK(!td::is_scheduler_alive());
}
