#include <nexus/test.hh>

#include <clean-core/vector.hh>

#include <babel-serializer/experimental/byte_writer.hh>

#include <typed-geometry/tg-lean.hh>

TEST("byte_writer")
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

    cc::vector<std::byte> bytes;

    auto w = babel::experimental::byte_writer([&](cc::span<std::byte const> data) { //
        bytes.push_back_range(data);
    });

    foo f;
    w.write_i32(f.i);
    w.write_bool(true);
    w.write_char('Z');
    w.write_u16(1000);
    w.write_u64(12345);
    w.write_f32(3.25f);
    w.write_pod(tg::pos3(1.5f, 2.5f, 3.5f));
    w.write_f64(-5.125f);

    auto f_bytes = cc::vector<std::byte>(cc::as_byte_span(f));
    CHECK(f_bytes == bytes);
}
