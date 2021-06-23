#include <nexus/test.hh>

#include <texture-processor/image.hh>
#include <texture-processor/image_view.hh>

TEST("tp traits")
{
    static_assert(tp::is_image_view<tp::image2_view<float>>);
    static_assert(!tp::is_image<tp::image2_view<float>>);

    static_assert(!tp::is_image_view<tp::image2<float>>);
    static_assert(tp::is_image<tp::image2<float>>);

    static_assert(tp::is_image_or_view<tp::image2<float>>);
    static_assert(tp::is_image_or_view<tp::image2_view<float>>);

    CHECK(true); // uses static_asserts
}
