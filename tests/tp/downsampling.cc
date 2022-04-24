#include <nexus/test.hh>

#include <texture-processor/feature/resampling.hh>

TEST("downsampling basics")
{
    CHECK(tp::detail::downsampled_extent<0>(tp::extent2{100, 51}) == tp::extent2{50, 51});
    CHECK(tp::detail::downsampled_extent<1>(tp::extent2{100, 51}) == tp::extent2{100, 25});
    CHECK(tp::detail::downsampled_extent<0, 1>(tp::extent2{100, 51}) == tp::extent2{50, 25});
    CHECK(tp::detail::downsampled_extent<0, 1>(tp::extent3{100, 51, 20}) == tp::extent3{50, 25, 20});
    CHECK(tp::detail::downsampled_extent<0, 2>(tp::extent3{100, 51, 20}) == tp::extent3{50, 51, 10});
}
