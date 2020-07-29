#include <nexus/fuzz_test.hh>

#include <clean-core/bits.hh>

TEST("bits")
{
    // popcnt
    CHECK(cc::popcount(cc::uint8(0b1001101)) == 4);
    CHECK(cc::popcount(cc::uint16(0b1001101)) == 4);
    CHECK(cc::popcount(cc::uint32(0b1001101)) == 4);
    CHECK(cc::popcount(cc::uint64(0b1001101)) == 4);
    CHECK(cc::popcount(cc::uint32(0)) == 0);

    // lzcnt
    CHECK(cc::count_leading_zeros(cc::uint8(0b0101)) == 5);
    CHECK(cc::count_leading_zeros(cc::uint16(0b0101)) == 13);
    CHECK(cc::count_leading_zeros(cc::uint32(0b0101)) == 29);
    CHECK(cc::count_leading_zeros(cc::uint64(0b0101)) == 61);

    CHECK(cc::count_leading_zeros(cc::uint8(-1)) == 0);
    CHECK(cc::count_leading_zeros(cc::uint16(-1)) == 0);
    CHECK(cc::count_leading_zeros(cc::uint32(-1)) == 0);
    CHECK(cc::count_leading_zeros(cc::uint64(-1)) == 0);

    CHECK(cc::count_leading_zeros(cc::uint8(0)) == 8);
    CHECK(cc::count_leading_zeros(cc::uint16(0)) == 16);
    CHECK(cc::count_leading_zeros(cc::uint32(0)) == 32);
    CHECK(cc::count_leading_zeros(cc::uint64(0)) == 64);

    // tzcnt
    CHECK(cc::count_trailing_zeros(cc::uint32(0b1011000)) == 3);
    CHECK(cc::count_trailing_zeros(cc::uint64(0b1011000)) == 3);

    CHECK(cc::count_trailing_zeros(cc::uint32(0)) == 32);
    CHECK(cc::count_trailing_zeros(cc::uint64(0)) == 64);

    CHECK(cc::count_trailing_zeros(cc::uint32(-1)) == 0);
    CHECK(cc::count_trailing_zeros(cc::uint64(-1)) == 0);

    // pow2/log2 utilities
    CHECK(cc::bit_log2(cc::uint32(1)) == 0);
    CHECK(cc::bit_log2(cc::uint32(2)) == 1);
    CHECK(cc::bit_log2(cc::uint32(3)) == 1);
    CHECK(cc::bit_log2(cc::uint32(4)) == 2);
    CHECK(cc::bit_log2(cc::uint32(1024)) == 10);
    CHECK(cc::bit_log2(cc::uint32(1) << 31) == 31);

    CHECK(cc::bit_log2(cc::uint64(1)) == 0);
    CHECK(cc::bit_log2(cc::uint64(2)) == 1);
    CHECK(cc::bit_log2(cc::uint64(3)) == 1);
    CHECK(cc::bit_log2(cc::uint64(4)) == 2);
    CHECK(cc::bit_log2(cc::uint64(1024)) == 10);
    CHECK(cc::bit_log2(cc::uint64(1) << 63) == 63);

    CHECK(cc::ceil_pow2(cc::uint32(0)) == 1);
    CHECK(cc::ceil_pow2(cc::uint32(1)) == 1);
    CHECK(cc::ceil_pow2(cc::uint32(2)) == 2);
    CHECK(cc::ceil_pow2(cc::uint32(3)) == 4);
    CHECK(cc::ceil_pow2(cc::uint32(4)) == 4);
    CHECK(cc::ceil_pow2(cc::uint32(5)) == 8);

    CHECK(cc::is_pow2(cc::uint32(1)));
    CHECK(cc::is_pow2(cc::uint32(2)));
}

FUZZ_TEST("bits fuzz")(tg::rng& rng)
{
    int const exp_nonzero = tg::uniform(rng, 4, 30);

    CHECK(cc::count_trailing_zeros(cc::uint32(1) << exp_nonzero) == exp_nonzero);
    CHECK(cc::count_leading_zeros(cc::uint32(-1) >> exp_nonzero) == exp_nonzero);

    cc::uint32 const pow2_clamped = cc::uint32(1) << exp_nonzero;

    CHECK(cc::popcount(pow2_clamped) == 1);

    CHECK(cc::bit_log2(pow2_clamped) == exp_nonzero);
    CHECK(cc::bit_log2(pow2_clamped - 1) == exp_nonzero - 1);

    CHECK(cc::is_pow2(pow2_clamped));
    CHECK(!cc::is_pow2(pow2_clamped - 1));
    CHECK(!cc::is_pow2(pow2_clamped + 1));
}
