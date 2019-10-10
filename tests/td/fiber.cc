#include <doctest.hh>

#include <atomic>

#include <task-dispatcher/native/fiber.hh>

struct SingleFiberArg
{
    std::atomic_long Counter{0};
    td::native::fiber_t mainFiber;
    td::native::fiber_t otherFiber;
};

void SingleFiberStart(void* arg)
{
    auto* const singleFiberArg = reinterpret_cast<SingleFiberArg*>(arg);

    singleFiberArg->Counter.fetch_add(1);
    td::native::switch_to_fiber(singleFiberArg->mainFiber, singleFiberArg->otherFiber);

    // We should never get here
    CHECK(false);
}

TEST_CASE("td::native::fiber")
{
    auto constexpr kHalfMebibyte = 524288;

    SingleFiberArg singleFiberArg;
    singleFiberArg.Counter.store(0);
    td::native::create_main_fiber(singleFiberArg.mainFiber);

    td::native::create_fiber(singleFiberArg.otherFiber, SingleFiberStart, &singleFiberArg, kHalfMebibyte);

    td::native::switch_to_fiber(singleFiberArg.otherFiber, singleFiberArg.mainFiber);

    td::native::delete_fiber(singleFiberArg.otherFiber);
    td::native::delete_main_fiber(singleFiberArg.mainFiber);

    CHECK_EQ(singleFiberArg.Counter.load(), 1);
}
