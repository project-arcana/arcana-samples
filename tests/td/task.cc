#include <doctest.hh>

#include <array>
#include <iostream>
#include <limits>
#include <memory>

#include <task-dispatcher/container/task.hh>

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
        // Check if the task didn't run yet
        CHECK(!taskExecuted);

        task.executeAndCleanup();

        // Check if execute_and_cleanup destroyed the lambda capture
        CHECK(weakInt.expired());
        // Check if the task ran
        CHECK(taskExecuted);
    }
#undef SHARED_INT_VALUE
}

TEST_CASE("td::container::Task (metadata)")
{
    // Constants are not constexpr variables because
    // lambda captures are a concern of this test.
    // Could be randomized.
#define TASK_CANARAY_INITIAL 20
#define CAPTURE_PAD_SIZE 32

    td::container::Task task;

    auto constexpr metaMin = std::numeric_limits<uint16_t>().min();
    auto constexpr metaMax = std::numeric_limits<uint16_t>().max();
    for (auto testMetadata : {metaMin, metaMax})
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
