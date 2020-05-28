#include <nexus/test.hh>

#include <vector>

#include <task-dispatcher/scheduler.hh>

using namespace td;

namespace
{
auto constexpr workloadSize = 5000000;
std::vector<int> sGlobalBuffer;

auto constexpr chunkSize = 10000;
auto constexpr numWorkers = workloadSize / chunkSize;

void mainTaskFunc(void*)
{
    container::task workers[numWorkers];
    for (auto i = 0u; i < numWorkers; ++i)
    {
        unsigned chunkStart = i * chunkSize;
        unsigned chunkEnd = (i + 1) * chunkSize;

        workers[i].lambda([chunkStart, chunkEnd]() {
            for (auto i = chunkStart; i < chunkEnd; ++i)
            {
                sGlobalBuffer[i] = int(i);
            }
        });
    }

    sync sync;
    auto& sched = Scheduler::Current();
    sched.submitTasks(workers, numWorkers, sync);
    sched.wait(sync);
}
}


TEST("td::Scheduler (dependency)", exclusive)
{
    sGlobalBuffer.resize(workloadSize, 0);
    std::fill(sGlobalBuffer.begin(), sGlobalBuffer.end(), 0);

    Scheduler scheduler;
    scheduler.start(container::task{mainTaskFunc});

    bool equal = true;
    for (auto i = 0u; i < workloadSize; ++i)
        equal = equal && sGlobalBuffer[i] == int(i);

    CHECK(equal);

    sGlobalBuffer.clear();
    sGlobalBuffer.shrink_to_fit();
}
