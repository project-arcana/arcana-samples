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

    CHECK(cc::has_bit(cc::uint8(0b0101), 0) == true);
    CHECK(cc::has_bit(cc::uint16(0b0101), 0) == true);
    CHECK(cc::has_bit(cc::uint32(0b0101), 0) == true);
    CHECK(cc::has_bit(cc::uint64(0b0101), 0) == true);

    CHECK(cc::has_bit(cc::uint8(0b0101), 1) == false);
    CHECK(cc::has_bit(cc::uint16(0b0101), 1) == false);
    CHECK(cc::has_bit(cc::uint32(0b0101), 1) == false);
    CHECK(cc::has_bit(cc::uint64(0b0101), 1) == false);

    CHECK(cc::has_bit(0b1100u, 2));
    CHECK(cc::has_bit(0b1100u, 3));
    CHECK(cc::has_bit(cc::uint8(0xFF), 7));
    CHECK(cc::has_bit(cc::uint32(0xFFFFFFFF), 31));

    auto f_test_has_no_bits = [](auto val) {
        for (auto i = 0; i < sizeof(val) * 8; ++i)
        {
            if (cc::has_bit(val, i))
            {
                return false;
            }
        }

        return true;
    };

    CHECK(f_test_has_no_bits(cc::uint8(0)));
    CHECK(f_test_has_no_bits(cc::uint16(0)));
    CHECK(f_test_has_no_bits(cc::uint32(0)));
    CHECK(f_test_has_no_bits(cc::uint64(0)));

    auto f_test_has_all_bits = [](auto val) {
        for (auto i = 0; i < sizeof(val) * 8; ++i)
        {
            if (!cc::has_bit(val, i))
            {
                return false;
            }
        }

        return true;
    };

    CHECK(f_test_has_all_bits(cc::uint8(-1)));
    CHECK(f_test_has_all_bits(cc::uint16(-1)));
    CHECK(f_test_has_all_bits(cc::uint32(-1)));
    CHECK(f_test_has_all_bits(cc::uint64(-1)));

    CHECK(cc::div_pow2_floor(0u, 1u) == 0u);
    CHECK(cc::div_pow2_floor(0u, 2u) == 0u);
    CHECK(cc::div_pow2_floor(0u, 4u) == 0u);
    CHECK(cc::div_pow2_floor(0u, 32u) == 0u);
    CHECK(cc::div_pow2_floor(0u, 512u) == 0u);

    CHECK(cc::div_pow2_floor(1u, 1u) == 1u);
    CHECK(cc::div_pow2_floor(2u, 2u) == 1u);
    CHECK(cc::div_pow2_floor(4u, 4u) == 1u);
    CHECK(cc::div_pow2_floor(32u, 32u) == 1u);
    CHECK(cc::div_pow2_floor(512u, 512u) == 1u);

    CHECK(cc::div_pow2_floor(512u, 256u) == 2u);
    CHECK(cc::div_pow2_floor(512u, 128u) == 4u);
    CHECK(cc::div_pow2_floor(512u, 4u) == 128u);
    CHECK(cc::div_pow2_floor(512u, 2u) == 256u);

    CHECK(cc::div_pow2_floor(3u, 2u) == 1u);
    CHECK(cc::div_pow2_floor(5u, 4u) == 1u);
    CHECK(cc::div_pow2_floor(33u, 32u) == 1u);
    CHECK(cc::div_pow2_floor(513u, 512u) == 1u);

    CHECK(cc::div_pow2_floor(7u, 4u) == 1u);
    CHECK(cc::div_pow2_floor(63u, 32u) == 1u);
    CHECK(cc::div_pow2_floor(1023u, 512u) == 1u);

    CHECK(cc::div_pow2_ceil(1u, 1u) == 1u);
    CHECK(cc::div_pow2_ceil(2u, 2u) == 1u);
    CHECK(cc::div_pow2_ceil(4u, 4u) == 1u);
    CHECK(cc::div_pow2_ceil(512u, 512u) == 1u);

    CHECK(cc::div_pow2_ceil(512u, 256u) == 2u);
    CHECK(cc::div_pow2_ceil(512u, 128u) == 4u);
    CHECK(cc::div_pow2_ceil(512u, 4u) == 128u);
    CHECK(cc::div_pow2_ceil(512u, 2u) == 256u);

    CHECK(cc::div_pow2_ceil(3u, 2u) == 2u);
    CHECK(cc::div_pow2_ceil(5u, 4u) == 2u);
    CHECK(cc::div_pow2_ceil(33u, 32u) == 2u);
    CHECK(cc::div_pow2_ceil(513u, 512u) == 2u);

    CHECK(cc::div_pow2_ceil(7u, 4u) == 2u);
    CHECK(cc::div_pow2_ceil(63u, 32u) == 2u);
    CHECK(cc::div_pow2_ceil(1023u, 512u) == 2u);
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

    int const bit_idx32 = tg::uniform(rng, 0, 31);
    cc::uint32 u32 = 0u;

    CHECK(!cc::has_bit(u32, bit_idx32));
    cc::unset_bit(u32, bit_idx32);
    CHECK(!cc::has_bit(u32, bit_idx32));
    cc::set_bit(u32, bit_idx32);
    CHECK(cc::has_bit(u32, bit_idx32));
    cc::flip_bit(u32, bit_idx32);
    CHECK(!cc::has_bit(u32, bit_idx32));
    cc::flip_bit(u32, bit_idx32);
    CHECK(cc::has_bit(u32, bit_idx32));

    int const bit_idx64 = tg::uniform(rng, 0, 63);
    cc::uint64 u64 = 0u;
    CHECK(!cc::has_bit(u64, bit_idx64));
    cc::unset_bit(u64, bit_idx64);
    CHECK(!cc::has_bit(u64, bit_idx64));
    cc::set_bit(u64, bit_idx64);
    CHECK(cc::has_bit(u64, bit_idx64));
    cc::flip_bit(u64, bit_idx64);
    CHECK(!cc::has_bit(u64, bit_idx64));
    cc::flip_bit(u64, bit_idx64);
    CHECK(cc::has_bit(u64, bit_idx64));
}
