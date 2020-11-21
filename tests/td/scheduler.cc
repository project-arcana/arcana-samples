#include <nexus/test.hh>

#include <clean-core/array.hh>

#include <rich-log/log.hh>

#include <task-dispatcher/common/math_intrin.hh>
#include <task-dispatcher/scheduler.hh>

using namespace td;

namespace
{
auto constexpr spin_default = 25ull;

auto constexpr num_tasks_outer = 5u;
auto constexpr num_tasks_inner = 10u;

void spin_cycles(uint64_t cycles = spin_default)
{
    auto const current = intrin::rdtsc();
    while (intrin::rdtsc() - current < cycles)
    {
        _mm_pause();
    }
}

void outer_task_func(void*)
{
    std::atomic_int* const dependency = new std::atomic_int(0);

    auto s = Scheduler::Current().acquireCounterHandle();

    cc::array<container::task, num_tasks_inner> tasks;

    for (container::task& task : tasks)
    {
        task.lambda([dependency]() {
            spin_cycles();
            dependency->fetch_add(1);
        });
    }

    CHECK(dependency->load() == 0);
    Scheduler::Current().submitTasks(tasks.data(), unsigned(tasks.size()), s);

    Scheduler::Current().wait(s, true);
    Scheduler::Current().releaseCounter(s);

    CHECK(dependency->load() == num_tasks_inner);

    delete dependency;
}

template <int MaxIterations>
void main_task_func(void*)
{
    static_assert(MaxIterations > 0, "at least one iteration required");

    auto s = Scheduler::Current().acquireCounterHandle();

    for (auto iter = 0; iter < MaxIterations; ++iter)
    {
        cc::array<container::task, num_tasks_outer> tasks;

        for (container::task& task : tasks)
        {
            task.ptr(outer_task_func);
        }

        Scheduler::Current().submitTasks(tasks.data(), unsigned(tasks.size()), s);
        Scheduler::Current().wait(s, true);
    }

    Scheduler::Current().wait(s, true);
    int last_state = Scheduler::Current().releaseCounter(s);
    CHECK(last_state == 0);
}
}

TEST("td::Scheduler", exclusive)
{
    {
        scheduler_config config;

        // Make sure this test does not exceed the configured job limit
        REQUIRE((num_tasks_inner * num_tasks_outer) + 1 < config.max_num_tasks);
    }

    // Run a simple dependency chain
    {
        Scheduler scheduler;
        scheduler.start(container::task{main_task_func<1>, nullptr});
    }

    // Run multiple times
    {
        Scheduler scheduler;

        scheduler.start(container::task{main_task_func<25>, nullptr});
        scheduler.start(container::task{main_task_func<25>, nullptr});
    }

    // Run with constrained threads
    {
        scheduler_config config;
        config.num_threads = 2;

        Scheduler scheduler(config);

        scheduler.start(container::task{main_task_func<150>, nullptr});
    }
}
