#include <nexus/test.hh>

#include <iostream>
#include <random>

#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-hardware-interface/detail/hash.hh>
#include <phantasm-hardware-interface/detail/linked_pool.hh>
#include <phantasm-hardware-interface/detail/page_allocator.hh>


TEST("pr backend detail - page allocator")
{
    phi::detail::page_allocator allocator;
    allocator.initialize(80, 4);

    auto const alloc1 = allocator.allocate(4);
    CHECK(alloc1 == 0);

    auto const alloc2 = allocator.allocate(4);
    CHECK(alloc2 == 1);

    auto const alloc3 = allocator.allocate(7);
    CHECK(alloc3 == 2);

    auto const alloc4 = allocator.allocate(9);
    CHECK(alloc4 == 4);

    auto const alloc5 = allocator.allocate(1);
    CHECK(alloc5 == 7);

    allocator.free(alloc2);
    auto const alloc6 = allocator.allocate(5);
    CHECK(alloc6 == 8);
    auto const alloc7 = allocator.allocate(3);
    CHECK(alloc7 == 1);
}

TEST("pr backend detail - linked pool")
{
    struct node
    {
        int x;
        int y;
    };

    phi::detail::linked_pool<node> pool;
    pool.initialize(50);

    auto const i1 = pool.acquire();
    CHECK(i1 == 0);
    node& n1 = pool.get(i1);
    n1 = {5, 7};

    auto const i2 = pool.acquire();
    CHECK(i2 == 1);

    CHECK(n1.x == 5);
    pool.release(i1);
    CHECK(n1.x != 5); // NOTE: this read is UB

    auto const i3 = pool.acquire();
    CHECK(i3 == 0);
}

TEST("pr backend detail - page allocator random", disabled)
{
    constexpr auto allocation_size = 32;
    constexpr auto allocation_chance = 0.85f;

    phi::detail::page_allocator allocator;
    allocator.initialize(1024, 16);
    tg::rng rng(std::random_device{});

    cc::vector<int> allocations;
    allocations.reserve(1024);

    auto num_total_allocs = 0;

    while (true)
    {
        if (tg::uniform(rng, 0.f, 1.f) < allocation_chance)
        {
            auto const alloc_size = tg::uniform(rng, 1, allocation_size);

            auto const res = allocator.allocate(alloc_size);

            std::cout << "Allocated " << alloc_size << " elements in page " << res << std::endl;

            if (res == -1)
                break;

            allocations.push_back(res);
            ++num_total_allocs;
        }
        else
        {
            if (allocations.empty())
                continue;

            auto const free_index = tg::uniform(rng, size_t(0), allocations.size() - 1);

            allocator.free(allocations[free_index]);

            std::cout << "Freed allocation at page " << allocations[free_index] << std::endl;

            allocations[free_index] = allocations.back();
            allocations.pop_back();
        }
    }

    std::cout << "Overcommitted after " << num_total_allocs << " total allocations" << std::endl;
}