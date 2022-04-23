#include <nexus/fuzz_test.hh>

#include <rich-log/log.hh>

#include <phantasm-hardware-interface/common/byte_util.hh>
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

// disabled - these values are wrong because they do not respect per-subresource 512B alignment plus this test
// uses the deprecated helper that does not support block-compressed textures and array configurations
#if 0
TEST("upload sizes")
{
    //    auto const f_test_upsize = [](tg::isize3 size, phi::format fmt) {
    //        LOG_INFO("{}, format {}:", size, uint8_t(fmt));
    //        LOG_INFO("m0 d12: {}, vk: {}", phi::util::get_texture_size_bytes(size, fmt, 0, true), phi::util::get_texture_size_bytes(size, fmt, 0, false));
    //        LOG_INFO("m1 d12: {}, vk: {}", phi::util::get_texture_size_bytes(size, fmt, 1, true), phi::util::get_texture_size_bytes(size, fmt, 1, false));
    //    };

    //    f_test_upsize({90128, 28561, 7}, phi::format::rgb32i);

    // LOG_INFO("w^8: {}", 1u << 8);

    // 4 byte pixels
    // in order: all mips d3d12, all mips vk, no mips d3d12, no mips vk

    CHECK(phi::util::get_texture_size_bytes({256, 256, 1}, phi::format::bgra8un, 0, true) == 360192);
    CHECK(phi::util::get_texture_size_bytes({256, 256, 1}, phi::format::bgra8un, 0, false) == 349524);
    CHECK(phi::util::get_texture_size_bytes({256, 256, 1}, phi::format::bgra8un, 1, true) == 262144);
    CHECK(phi::util::get_texture_size_bytes({256, 256, 1}, phi::format::bgra8un, 1, false) == 262144);

    CHECK(phi::util::get_texture_size_bytes({1024, 1024, 1}, phi::format::bgra8un, 0, true) == 5603072);
    CHECK(phi::util::get_texture_size_bytes({1024, 1024, 1}, phi::format::bgra8un, 0, false) == 5592404);
    CHECK(phi::util::get_texture_size_bytes({1024, 1024, 1}, phi::format::bgra8un, 1, true) == 4194304);
    CHECK(phi::util::get_texture_size_bytes({1024, 1024, 1}, phi::format::bgra8un, 1, false) == 4194304);

    CHECK(phi::util::get_texture_size_bytes({57, 43, 1}, phi::format::bgra8un, 0, true) == 20992);
    CHECK(phi::util::get_texture_size_bytes({57, 43, 1}, phi::format::bgra8un, 0, false) == 12884);
    CHECK(phi::util::get_texture_size_bytes({57, 43, 1}, phi::format::bgra8un, 1, true) == 11008);
    CHECK(phi::util::get_texture_size_bytes({57, 43, 1}, phi::format::bgra8un, 1, false) == 9804);

    CHECK(phi::util::get_texture_size_bytes({57, 43, 2}, phi::format::bgra8un, 0, true) == 41984);
    CHECK(phi::util::get_texture_size_bytes({57, 43, 2}, phi::format::bgra8un, 0, false) == 25768);
    CHECK(phi::util::get_texture_size_bytes({57, 43, 2}, phi::format::bgra8un, 1, true) == 22016);
    CHECK(phi::util::get_texture_size_bytes({57, 43, 2}, phi::format::bgra8un, 1, false) == 19608);

    CHECK(phi::util::get_texture_size_bytes({256, 256, 4}, phi::format::bgra8un, 0, true) == 1440768);
    CHECK(phi::util::get_texture_size_bytes({256, 256, 4}, phi::format::bgra8un, 0, false) == 1398096);
    CHECK(phi::util::get_texture_size_bytes({256, 256, 4}, phi::format::bgra8un, 1, true) == 1048576);
    CHECK(phi::util::get_texture_size_bytes({256, 256, 4}, phi::format::bgra8un, 1, false) == 1048576);

    // 8 byte pixels

    CHECK(phi::util::get_texture_size_bytes({256, 256, 1}, phi::format::rgba16f, 0, true) == 704256);
    CHECK(phi::util::get_texture_size_bytes({256, 256, 1}, phi::format::rgba16f, 0, false) == 699048);
    CHECK(phi::util::get_texture_size_bytes({256, 256, 1}, phi::format::rgba16f, 1, true) == 524288);
    CHECK(phi::util::get_texture_size_bytes({256, 256, 1}, phi::format::rgba16f, 1, false) == 524288);

    CHECK(phi::util::get_texture_size_bytes({57, 43, 2}, phi::format::rgba16f, 0, true) == 64000);
    CHECK(phi::util::get_texture_size_bytes({57, 43, 2}, phi::format::rgba16f, 0, false) == 51536);
    CHECK(phi::util::get_texture_size_bytes({57, 43, 2}, phi::format::rgba16f, 1, true) == 44032);
    CHECK(phi::util::get_texture_size_bytes({57, 43, 2}, phi::format::rgba16f, 1, false) == 39216);

    // 12 byte pixels

    CHECK(phi::util::get_texture_size_bytes({90128, 28561, 7}, phi::format::rgb32i, 0, true) == 587038464);
    CHECK(phi::util::get_texture_size_bytes({90128, 28561, 7}, phi::format::rgb32i, 0, false) == 538751960);
    CHECK(phi::util::get_texture_size_bytes({90128, 28561, 7}, phi::format::rgb32i, 1, true) == 1492678400);
    CHECK(phi::util::get_texture_size_bytes({90128, 28561, 7}, phi::format::rgb32i, 1, false) == 1479883072);

    // 16 byte pixels

    CHECK(phi::util::get_texture_size_bytes({51241, 78823, 2}, phi::format::rgba32f, 0, true) == 572323328);
    CHECK(phi::util::get_texture_size_bytes({51241, 78823, 2}, phi::format::rgba32f, 0, false) == 528704480);
    CHECK(phi::util::get_texture_size_bytes({51241, 78823, 2}, phi::format::rgba32f, 1, true) == 415656448);
    CHECK(phi::util::get_texture_size_bytes({51241, 78823, 2}, phi::format::rgba32f, 1, false) == 398000096);
}
#endif

TEST("memory alignment")
{
    using namespace phi::util;

    CHECK(align_up(1u, 4u) == 4u);
    CHECK(align_up(4u, 4u) == 4u);
    CHECK(align_up(16u, 4u) == 16u);
    CHECK(align_up(16u, 32u) == 32u);
    CHECK(align_up(31u, 32u) == 32u);
    CHECK(align_up(33u, 32u) == 64u);
    CHECK(align_down(31u, 16u) == 16u);
    CHECK(align_down(32u, 16u) == 32u);

    alignas(16) int foo;
    CHECK(is_aligned(&foo, 16));

    alignas(32) int bar;
    CHECK(is_aligned(&bar, 32));

    CHECK(align_up<uint32_t>(0, 4) == 0);
    CHECK(align_up<int>(0, 4) == 0);
    CHECK(align_up<uint32_t>(1, 4) == 4);
    CHECK(align_up<int>(1, 4) == 4);
}
