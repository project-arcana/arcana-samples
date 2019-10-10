#include <doctest.hh>

#include <array>
#include <iostream>
#include <limits>
#include <memory>

#include <task-dispatcher/common/system_info.hh>
#include <task-dispatcher/container/task.hh>

namespace
{
int gSink = 0;
}

TEST_CASE("td::container::Task (lifetime)")
{
    // Constants are not constexpr variables because
    // lambda captures are a concern of this test.
    // Could be randomized.
#define SHARED_INT_VALUE 55

    td::container::Task task;
    std::weak_ptr<int> weakInt;

    for (auto i = 0; i < 3; ++i)
    {
        bool taskExecuted = false;

        {
            std::shared_ptr<int> sharedInt = std::make_shared<int>(SHARED_INT_VALUE);
            weakInt = sharedInt;

            task.lambda([sharedInt, &taskExecuted]() {
                // Check if sharedInt is alive and correct
                CHECK_EQ(*sharedInt, SHARED_INT_VALUE);

                taskExecuted = true;
            });

            // sharedInt runs out of scope
        }

        // Check if task kept the lambda capture alive
        CHECK(!weakInt.expired());
        CHECK(!taskExecuted);

        task.execute();

        CHECK(taskExecuted);
        CHECK(!weakInt.expired());

        task.cleanup();

        // Check if execute_and_cleanup destroyed the lambda capture
        CHECK(taskExecuted);
        CHECK(weakInt.expired());
    }
#undef SHARED_INT_VALUE
}

TEST_CASE("td::container::Task (static)")
{
    static_assert(sizeof(td::container::Task) == td::system::l1_cacheline_size);

    int a, b, c;
    auto uptr = std::make_unique<int>(1);

    auto l_trivial = [] { ++gSink; };
    auto l_ref_cap = [&] { gSink += (a - b + c); };
    auto l_val_cap = [=] { gSink += (a - b + c); };
    auto l_val_cap_mutable = [=]() mutable { gSink += (c += b); };
    auto l_noexcept = [&]() noexcept { gSink += (a - b + c); };
    auto l_constexpr = [=]() constexpr { gSink += (a - b + c); };
    auto l_noncopyable = [p = std::move(uptr)] { gSink += *p; };

    // Test if these lambda types compile
    td::container::Task(std::move(l_trivial)).executeAndCleanup();
    td::container::Task(std::move(l_ref_cap)).executeAndCleanup();
    td::container::Task(std::move(l_val_cap)).executeAndCleanup();
    td::container::Task(std::move(l_val_cap_mutable)).executeAndCleanup();
    td::container::Task(std::move(l_noexcept)).executeAndCleanup();
    td::container::Task(std::move(l_constexpr)).executeAndCleanup();
    td::container::Task(std::move(l_noncopyable)).executeAndCleanup();
}

TEST_CASE("td::container::Task (metadata)")
{
    // Constants are not constexpr variables because
    // lambda captures are a concern of this test.
    // Could be randomized.
#define TASK_CANARAY_INITIAL 20
#define CAPTURE_PAD_SIZE 32

    td::container::Task task;

    using metadata_t = td::container::Task::default_metadata_t;
    auto constexpr metaMin = std::numeric_limits<metadata_t>().min();
    auto constexpr metaMax = std::numeric_limits<metadata_t>().max();
    for (auto testMetadata : {metaMin, metadata_t(0), metaMax})
    {
        uint16_t taskRunCanary = TASK_CANARAY_INITIAL;

        std::array<char, CAPTURE_PAD_SIZE> pad;
        std::fill(pad.begin(), pad.end(), 0);

        task.setMetadata(testMetadata);

        // Test if the write was successful
        CHECK(task.getMetadata() == testMetadata);

        task.lambda([testMetadata, &taskRunCanary, pad]() {
            // Sanity
            CHECK(taskRunCanary == TASK_CANARAY_INITIAL);

            taskRunCanary = testMetadata;

            for (auto i = 0u; i < CAPTURE_PAD_SIZE; ++i)
            {
                // Check if the pad does not collide with the metadata
                CHECK(pad.at(i) == 0);
            }
        });

        // Test if the task write didn't compromise the metadata
        CHECK(task.getMetadata() == testMetadata);

        // Sanity
        CHECK(taskRunCanary == TASK_CANARAY_INITIAL);

        task.executeAndCleanup();

        // Test if the task ran correctly
        CHECK(taskRunCanary == testMetadata);

        // Test if the metadata survived
        CHECK(task.getMetadata() == testMetadata);
    }

#undef TASK_CANARAY_INITIAL
#undef CAPTURE_PAD_SIZE
}
