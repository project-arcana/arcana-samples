#include <nexus/test.hh>

#include <atomic>

#include <task-dispatcher/native/fiber.hh>

struct MultipleFiberArg
{
    uint64_t Counter{0};
    td::native::fiber_t MainFiber;
    td::native::fiber_t FirstFiber;
    td::native::fiber_t SecondFiber;
    td::native::fiber_t ThirdFiber;
    td::native::fiber_t FourthFiber;
    td::native::fiber_t FifthFiber;
    td::native::fiber_t SixthFiber;
};

void FirstLevelFiberStart(void* arg)
{
    auto* singleFiberArg = reinterpret_cast<MultipleFiberArg*>(arg);

    singleFiberArg->Counter += 8;
    td::native::switch_to_fiber(singleFiberArg->SecondFiber, singleFiberArg->FirstFiber);

    // Return from sixth
    // We just finished 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 1
    // Intermediate check
    CHECK(((((((0ULL + 8ULL) * 3ULL) + 7ULL) * 6ULL) - 9ULL) * 2ULL) == singleFiberArg->Counter);

    // Now run the rest of the sequence
    singleFiberArg->Counter *= 4;
    td::native::switch_to_fiber(singleFiberArg->FifthFiber, singleFiberArg->FirstFiber);

    // Return from fifth
    singleFiberArg->Counter += 1;
    td::native::switch_to_fiber(singleFiberArg->ThirdFiber, singleFiberArg->FirstFiber);

    // We should never get here
    CHECK(false);
}

void SecondLevelFiberStart(void* arg)
{
    auto* singleFiberArg = reinterpret_cast<MultipleFiberArg*>(arg);

    singleFiberArg->Counter *= 3;
    td::native::switch_to_fiber(singleFiberArg->ThirdFiber, singleFiberArg->SecondFiber);

    // Return from third
    singleFiberArg->Counter += 9;
    td::native::switch_to_fiber(singleFiberArg->FourthFiber, singleFiberArg->SecondFiber);

    // Return from fourth
    singleFiberArg->Counter += 7;
    td::native::switch_to_fiber(singleFiberArg->FifthFiber, singleFiberArg->SecondFiber);

    // We should never get here
    CHECK(false);
}

void ThirdLevelFiberStart(void* arg)
{
    auto* singleFiberArg = reinterpret_cast<MultipleFiberArg*>(arg);

    singleFiberArg->Counter += 7;
    td::native::switch_to_fiber(singleFiberArg->FourthFiber, singleFiberArg->ThirdFiber);

    // Return from first
    singleFiberArg->Counter *= 3;
    td::native::switch_to_fiber(singleFiberArg->SecondFiber, singleFiberArg->ThirdFiber);

    // Return from fifth
    singleFiberArg->Counter *= 6;
    td::native::switch_to_fiber(singleFiberArg->SixthFiber, singleFiberArg->ThirdFiber);

    // We should never get here
    CHECK(false);
}

void FourthLevelFiberStart(void* arg)
{
    auto* singleFiberArg = reinterpret_cast<MultipleFiberArg*>(arg);

    singleFiberArg->Counter *= 6;
    td::native::switch_to_fiber(singleFiberArg->FifthFiber, singleFiberArg->FourthFiber);

    // Return from second
    singleFiberArg->Counter += 8;
    td::native::switch_to_fiber(singleFiberArg->SixthFiber, singleFiberArg->FourthFiber);

    // Return from sixth
    singleFiberArg->Counter *= 5;
    td::native::switch_to_fiber(singleFiberArg->SecondFiber, singleFiberArg->FourthFiber);

    // We should never get here
    CHECK(false);
}

void FifthLevelFiberStart(void* arg)
{
    auto* singleFiberArg = reinterpret_cast<MultipleFiberArg*>(arg);

    singleFiberArg->Counter -= 9;
    td::native::switch_to_fiber(singleFiberArg->SixthFiber, singleFiberArg->FifthFiber);

    // Return from first
    singleFiberArg->Counter *= 5;
    td::native::switch_to_fiber(singleFiberArg->FirstFiber, singleFiberArg->FifthFiber);

    // Return from second
    singleFiberArg->Counter += 1;
    td::native::switch_to_fiber(singleFiberArg->ThirdFiber, singleFiberArg->FifthFiber);

    // We should never get here
    CHECK(false);
}

void SixthLevelFiberStart(void* arg)
{
    auto* singleFiberArg = reinterpret_cast<MultipleFiberArg*>(arg);

    singleFiberArg->Counter *= 2;
    td::native::switch_to_fiber(singleFiberArg->FirstFiber, singleFiberArg->SixthFiber);

    // Return from fourth
    singleFiberArg->Counter -= 9;
    td::native::switch_to_fiber(singleFiberArg->FourthFiber, singleFiberArg->SixthFiber);

    // Return from third
    singleFiberArg->Counter -= 3;
    td::native::switch_to_fiber(singleFiberArg->MainFiber, singleFiberArg->SixthFiber);

    // We should never get here
    CHECK(false);
}

TEST("td::native::fiber (nested)", exclusive)
{
    auto constexpr kHalfMebibyte = 524288;

    MultipleFiberArg singleFiberArg;
    singleFiberArg.Counter = 0ULL;
    td::native::create_main_fiber(singleFiberArg.MainFiber);

    td::native::create_fiber(singleFiberArg.FirstFiber, FirstLevelFiberStart, &singleFiberArg, kHalfMebibyte);
    td::native::create_fiber(singleFiberArg.SecondFiber, SecondLevelFiberStart, &singleFiberArg, kHalfMebibyte);
    td::native::create_fiber(singleFiberArg.ThirdFiber, ThirdLevelFiberStart, &singleFiberArg, kHalfMebibyte);
    td::native::create_fiber(singleFiberArg.FourthFiber, FourthLevelFiberStart, &singleFiberArg, kHalfMebibyte);
    td::native::create_fiber(singleFiberArg.FifthFiber, FifthLevelFiberStart, &singleFiberArg, kHalfMebibyte);
    td::native::create_fiber(singleFiberArg.SixthFiber, SixthLevelFiberStart, &singleFiberArg, kHalfMebibyte);

    // The order should be:
    // 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 1 -> 5 -> 1 -> 3 -> 2 -> 4 -> 6 -> 4 -> 2 -> 5 -> 3 -> 6 -> Main

    td::native::switch_to_fiber(singleFiberArg.FirstFiber, singleFiberArg.MainFiber);

    td::native::delete_fiber(singleFiberArg.FirstFiber);
    td::native::delete_fiber(singleFiberArg.SecondFiber);
    td::native::delete_fiber(singleFiberArg.ThirdFiber);
    td::native::delete_fiber(singleFiberArg.FourthFiber);
    td::native::delete_fiber(singleFiberArg.FifthFiber);
    td::native::delete_fiber(singleFiberArg.SixthFiber);
    td::native::delete_main_fiber(singleFiberArg.MainFiber);

    CHECK(((((((((((((((((((0ULL + 8ULL) * 3ULL) + 7ULL) * 6ULL) - 9ULL) * 2ULL) * 4) * 5) + 1) * 3) + 9) + 8) - 9) * 5) + 7) + 1) * 6) - 3)
          == singleFiberArg.Counter);
}
