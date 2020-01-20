#include <nexus/test.hh>

#include <clean-core/bits.hh>

TEST("bits")
{
    CHECK(cc::popcount(cc::uint8(0b1001101)) == 4);
    CHECK(cc::popcount(cc::uint16(0b1001101)) == 4);
    CHECK(cc::popcount(cc::uint32(0b1001101)) == 4);
    CHECK(cc::popcount(cc::uint64(0b1001101)) == 4);

    CHECK(cc::count_trailing_zeros(cc::uint8(0b1011000)) == 3);
    CHECK(cc::count_trailing_zeros(cc::uint16(0b1011000)) == 3);
    CHECK(cc::count_trailing_zeros(cc::uint32(0b1011000)) == 3);
    CHECK(cc::count_trailing_zeros(cc::uint64(0b1011000)) == 3);

    CHECK(cc::count_leading_zeros(cc::uint8(0b0101)) == 5);
    CHECK(cc::count_leading_zeros(cc::uint16(0b0101)) == 13);
    CHECK(cc::count_leading_zeros(cc::uint32(0b0101)) == 29);
    CHECK(cc::count_leading_zeros(cc::uint64(0b0101)) == 61);
}
