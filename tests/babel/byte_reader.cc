#include <nexus/test.hh>

#include <babel-serializer/experimental/byte_reader.hh>

#include <typed-geometry/tg-lean.hh>

TEST("byte_reader")
{
    struct foo
    {
        int i = -10;
        bool b = true;
        char c = 'Z';
        cc::uint16 u = 1000;
        size_t s = 12345;
        float f = 3.25f;
        tg::pos3 p = {1.5f, 2.5f, 3.5f};
        double d = -5.125;
    };

    foo f;
    auto r = babel::experimental::byte_reader(cc::as_byte_span(f));
    CHECK(r.read_i32() == -10);
    CHECK(r.read_bool() == true);
    CHECK(r.read_char() == 'Z');
    CHECK(r.read_u16() == 1000);
    CHECK(r.read_u64() == 12345);
    CHECK(r.read_f32() == 3.25f);
    CHECK(r.read_pod<tg::pos3>() == tg::pos3(1.5f, 2.5f, 3.5f));
    CHECK(r.read_f64() == -5.125);
    CHECK(!r.has_remaining_bytes());
}
