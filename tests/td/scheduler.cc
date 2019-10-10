#include <doctest.hh>

#include <iostream>

#include <task-dispatcher/common/math_intrin.hh>
#include <task-dispatcher/scheduler.hh>

using namespace td;

namespace
{
auto constexpr spin_default = 25ull;

auto constexpr num_tasks_outer = 30;
auto constexpr num_tasks_inner = 30;

void spin_cycles(uint64_t cycles = spin_default)
{
    auto const current = intrin::rdtsc();
    while (intrin::rdtsc() - current < cycles)
        ; // Spin
}

void outer_task_func(void*)
{
    std::atomic_uint dependency = 0;

    auto& sched = Scheduler::current();
    sync s;

    auto tasks = new container::Task[num_tasks_inner];
    for (auto i = 0u; i < num_tasks_inner; ++i)
        tasks[i].lambda([&dependency]() {
            spin_cycles();
            dependency.fetch_add(1);
        });
    sched.submitTasks(tasks, num_tasks_inner, s);
    delete[] tasks;

    sched.wait(s);

    CHECK_EQ(dependency.load(), num_tasks_inner);
}

void main_task_func(void* arg_void)
{
    int const max_iterations = *static_cast<int*>(arg_void);

    for (auto iter = 0; iter < max_iterations; ++iter)
    {
        sync s;
        auto& sched = Scheduler::current();

        auto tasks = new container::Task[num_tasks_outer];
        for (auto i = 0u; i < num_tasks_outer; ++i)
            tasks[i].ptr(outer_task_func);
        sched.submitTasks(tasks, num_tasks_outer, s);
        delete[] tasks;

        sched.wait(s);
    }
}
}

TEST_CASE("td::Scheduler")
{
    {
        scheduler_config config;

        // Make sure this test does not exceed the configured job limit
        REQUIRE((num_tasks_inner * num_tasks_outer) + 1 < config.max_num_jobs);
    }

    // Run a simple dependency chain
    {
        Scheduler scheduler;
        auto iterations = 1;

        scheduler.start(container::Task{main_task_func, &iterations});
    }

    // Run multiple times
    {
        Scheduler scheduler;
        auto iterations = 25;

        scheduler.start(container::Task{main_task_func, &iterations});
        scheduler.start(container::Task{main_task_func, &iterations});
    }

    // Run with constrained threads
    {
        scheduler_config config;
        config.num_threads = 4;

        Scheduler scheduler(config);
        auto iterations = 150;

        scheduler.start(container::Task{main_task_func, &iterations});
    }
}
