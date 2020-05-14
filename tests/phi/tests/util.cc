#include <nexus/fuzz_test.hh>

#include <phantasm-hardware-interface/util.hh>

TEST("mipsize utils")
{
    // simple pow2 square
    auto const pow2_square = tg::isize2{1024, 1024};
    CHECK(phi::util::get_num_mips(pow2_square) == 11);

    CHECK(phi::util::get_mip_size(pow2_square, 0) == tg::isize2{1024, 1024});
    CHECK(phi::util::get_mip_size(pow2_square, 1) == tg::isize2{512, 512});
    CHECK(phi::util::get_mip_size(pow2_square, 2) == tg::isize2{256, 256});

    CHECK(phi::util::get_mip_size(pow2_square, 9) == tg::isize2{2, 2});
    CHECK(phi::util::get_mip_size(pow2_square, 10) == tg::isize2{1, 1});
    CHECK(phi::util::get_mip_size(pow2_square, 11) == tg::isize2{1, 1});

    // non-pow2, non-square
    auto const crooked = tg::isize2{57, 43};
    CHECK(phi::util::get_num_mips(crooked) == 6);

    CHECK(phi::util::get_mip_size(crooked, 0) == tg::isize2{57, 43});
    CHECK(phi::util::get_mip_size(crooked, 1) == tg::isize2{28, 21});
    CHECK(phi::util::get_mip_size(crooked, 2) == tg::isize2{14, 10});
    CHECK(phi::util::get_mip_size(crooked, 3) == tg::isize2{7, 5});
    CHECK(phi::util::get_mip_size(crooked, 4) == tg::isize2{3, 2});
    CHECK(phi::util::get_mip_size(crooked, 5) == tg::isize2{1, 1});
    CHECK(phi::util::get_mip_size(crooked, 6) == tg::isize2{1, 1});
    CHECK(phi::util::get_mip_size(crooked, 7) == tg::isize2{1, 1});

    CHECK(phi::util::get_num_mips(1, 1) == 1);
}

FUZZ_TEST("mipsize utils fuzz")(tg::rng& rng)
{
    int const random = tg::uniform(rng, 4, 1'000'000);

    auto const num_mips = phi::util::get_num_mips(random, random);

    CHECK(phi::util::get_mip_size(random, 0) == random);
    CHECK(phi::util::get_mip_size(random, 1) < random);
    CHECK(phi::util::get_mip_size(random, num_mips - 2) > 1);
    CHECK(phi::util::get_mip_size(random, num_mips - 1) == 1);
    CHECK(phi::util::get_mip_size(random, num_mips) == 1);
}
