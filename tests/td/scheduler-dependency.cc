#include <nexus/test.hh>

#include <vector>

#include <clean-core/span.hh>

#include <task-dispatcher/container/task.hh>
#include <task-dispatcher/scheduler.hh>
#include <task-dispatcher/sync.hh>

using namespace td;

namespace
{
auto constexpr workloadSize = 5000000;
auto constexpr chunkSize = 10000;
auto constexpr numWorkers = workloadSize / chunkSize;

struct GlobalBuffer
{
    std::vector<int> data;
};

void mainTaskFunc(void* arg)
{
    GlobalBuffer* const buf = static_cast<GlobalBuffer*>(arg);

    auto workers = std::vector<container::task>(numWorkers);

    for (auto i = 0u; i < numWorkers; ++i)
    {
        unsigned const chunkStart = i * chunkSize;
        unsigned const chunkEnd = (i + 1) * chunkSize;

        workers[i].lambda(
            [buf, chunkStart, chunkEnd]()
            {
                for (auto i = chunkStart; i < chunkEnd; ++i)
                {
                    buf->data[i] = int(i);
                }
            });
    }

    auto sync = td::acquireCounter();
    td::submitTasks(sync, workers);

    td::waitForCounter(sync);
    td::releaseCounter(sync);
}
}


TEST("td::Scheduler (dependency)", exclusive)
{
    GlobalBuffer globalBuf;
    globalBuf.data.resize(workloadSize, 0);
    std::fill(globalBuf.data.begin(), globalBuf.data.end(), 0);

    td::launchScheduler(td::scheduler_config{}, container::task{mainTaskFunc, &globalBuf});

    bool equal = true;
    for (auto i = 0u; i < workloadSize; ++i)
        equal = equal && globalBuf.data[i] == int(i);

    CHECK(equal);
}
