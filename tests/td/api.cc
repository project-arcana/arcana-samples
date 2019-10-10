#include <doctest.hh>

#include <array>
#include <iostream>
#include <thread>

#include <task-dispatcher/common/math_intrin.hh>
#include <task-dispatcher/td.hh>

namespace
{
int gSink = 0;

void argfun(int a, int b, int c)
{
    //    printf("Argfun: %d %d %d \n", a, b, c);
}

struct Foo
{
    void process_args(int a, int b)
    {
        //        printf("Foo process %d %d \n", a, b);
    }
};

template <class F, class FObj, class... Args>
void execute(F&& func, FObj& inst, Args&&... args)
{
    (inst.*func)(args...);
}

int calculate_fibonacci(int n)
{
    if (n < 2)
        return n;
    else
    {
        auto f1 = td::submit(calculate_fibonacci, n - 1);
        auto f2 = td::submit(calculate_fibonacci, n - 2);
        return f1.get_unpinned() + f2.get_unpinned();
    }
}

double fac(double num)
{
    double result = 1.0;
    for (double i = 2.0; i < num; i++)
        result *= i;
    return result;
}

double chudnovsky(double k_start, double k_end)
{
    auto res = 0.0;
    for (double k = k_start; k < k_end; k++)
    {
        res += (pow(-1.0, k) * fac(6.0 * k) * (13591409.0 + (545140134.0 * k))) / (fac(3.0 * k) * pow(fac(k), 3.0) * pow(640320.0, 3.0 * k + 3.0 / 2.0));
    }
    return res * 12.0;
}

double calculate_pi(int k, int num_batches_target)
{
    auto batch_size = td::int_div_ceil(k, num_batches_target);
    auto num_batches = td::int_div_ceil(k, batch_size);

    auto batchStorage = new double[num_batches];
    for (auto i = 0; i < num_batches; ++i)
        batchStorage[i] = 0.0;

    auto sync = td::submit_n(
        [batchStorage, batch_size](auto i) {
            auto k_start = double(i * batch_size);
            auto k_end = k_start + double(batch_size);
            batchStorage[i] = chudnovsky(k_start, k_end);
        },
        num_batches);

    td::wait_for_unpinned(sync);

    auto res = 0.0;
    for (auto i = 0; i < num_batches; ++i)
        res += batchStorage[i];

    delete[] batchStorage;

    return 1.0 / res;
}

}

TEST_CASE("td API")
{
    td::scheduler_config config;

    td::launch(config, [&] {
        {
            auto s1 = td::submit([] {});
            td::submit(s1, [] {});
            td::submit(s1, argfun, 1, 2, 3);

            // TODO
            //        Foo f;
            //        td::submit(s1, &Foo::process_args, f, 15, 16);

            auto s2 = td::submit([] {});
            auto s3 = td::submit([] {});

            CHECK(s1.initialized);
            CHECK(s2.initialized);
            CHECK(s3.initialized);

            td::wait_for_unpinned(s1, s2, s3);

            CHECK(!s1.initialized);
            CHECK(!s2.initialized);
            CHECK(!s3.initialized);
        }


        for (auto _ = 0; _ < 10; ++_)
        {
            auto largeTaskAmount = new td::container::Task[config.max_num_jobs];
            for (auto i = 0u; i < config.max_num_jobs; ++i)
                largeTaskAmount[i].lambda([] { /* no-op */ });

            auto before = td::intrin::rdtsc();
            auto s = td::submit_raw(largeTaskAmount, config.max_num_jobs);
            auto after = td::intrin::rdtsc();

            //            std::cout << "Average dispatch time: " << (after - before) / double(config.max_num_jobs) << " cycles" << std::endl;

            delete[] largeTaskAmount;

            auto before_wait = td::intrin::rdtsc();
            td::wait_for_unpinned(s);
            auto after_wait = td::intrin::rdtsc();

            //            std::cout << "Wait time: " << (after_wait - before_wait) << " cycles" << std::endl;
        }


        {
            auto f1 = td::submit([] { return 5.f * 15.f; });
            //            std::cout << "Future 1: " << f1.get() << std::endl;

            auto pi = calculate_pi(10000, 64);
            //            std::cout << "PI: " << pi << std::endl;
        }

        {
            td::sync s2;

            td::submit(s2, [] {
                //                printf("Task 2 start \n");

                auto s2_i = td::submit_n([](auto i) { /*printf("Task 2 - inner %d \n", i);*/ }, 4);

                td::wait_for_unpinned(s2_i);

                //                printf("Task 2 end \n");
            });

            td::submit(s2, [] {});

            td::wait_for_unpinned(s2);
        }

        {
            td::sync s3;
            td::submit_n(
                s3,
                [](auto i) {
                    //                    printf("Task 4 - %d start \n", i);

                    auto s4_i = td::submit_n(
                        [i](auto i_inner) {
                            //
                            //                            printf("Task 4 - %d - inner %d \n", i, i_inner);
                        },
                        4);

                    //                    printf("Task 4 - %d wait \n", i);
                    td::wait_for_unpinned(s4_i);
                },
                4);

            td::wait_for_unpinned(s3);
        }
    });
}

TEST_CASE("td API - general")
{
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
                    CHECK_EQ(a, 0);
                    CHECK_EQ(b, 0);
                    CHECK_EQ(c, 0);
                });

                td::wait_for(s2);

                a = 1;
                b = 2;
                c = 3;
            });

            td::submit(s3, [&] {
                td::wait_for(s1);
                CHECK_EQ(a, 1);
                CHECK_EQ(b, 2);
                CHECK_EQ(c, 3);
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
                CHECK_EQ(res_array[i], 0);
                if (i < n - 1)
                {
                    // TODO: Why doesn't this compile with [&res_array, i] ?
                    td::submit(syncs[i], [dat_ptr = res_array.data(), i] { dat_ptr[i] = i; });
                }

                if (i > 0)
                {
                    td::wait_for(syncs[i - 1]);
                    CHECK_EQ(res_array[i - 1], i - 1);
                }
            }

            td::wait_for(syncs[n - 1]);
            CHECK(std::this_thread::get_id() == main_thread_id);
        }

        // Number of executions
        {
            auto constexpr n = 500;
            std::atomic_int counter = {0};
            CHECK_EQ(counter.load(), 0);
            auto s = td::submit_n([&](auto) { ++counter; }, n);
            td::wait_for(s);
            CHECK_EQ(counter.load(), n);
            CHECK(std::this_thread::get_id() == main_thread_id);
        }
    });

    CHECK(!td::is_scheduler_alive());
}
