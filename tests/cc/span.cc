#include <nexus/test.hh>

#include <clean-core/span.hh>
#include <clean-core/vector.hh>

#include <clean-ranges/range.hh>

TEST("cc::span")
{
    cc::vector<int> v = {1, 2, 3};

    auto s = cc::span(v);
    CHECK(s.size() == 3);
    CHECK(s[0] == 1);
    CHECK(s[2] == 3);

    s = s.subspan(1, 2);
    CHECK(s.size() == 2);
    CHECK(s[0] == 2);
    CHECK(s[1] == 3);

    int va[] = {3, 2, 5, 6};
    s = cc::span(va);
    CHECK(s.size() == 4);
    CHECK(s[0] == 3);
    CHECK(s[3] == 6);
    s[1] += 2;
    CHECK(va[1] == 4);

    int x = 8;
    s = cc::span(x);
    CHECK(s.size() == 1);
    CHECK(s[0] == 8);
    x = 9;
    CHECK(s[0] == 9);

    s = {v.data(), v.size()};
    CHECK(s.size() == 3);
    CHECK(s[0] == 1);
    CHECK(s[2] == 3);

    s = {v.begin(), v.end()};
    CHECK(s.size() == 3);
    CHECK(s[0] == 1);
    CHECK(s[2] == 3);

    s = s.subspan(2);
    CHECK(s.size() == 1);
    CHECK(s[0] == 3);

    s = s.subspan(1);
    CHECK(s.size() == 0);
    CHECK(s.empty());

    s = v;
    CHECK(s.size() == 3);

    s = s.first(2);
    CHECK(cr::range(s) == cc::vector{1, 2});

    s = cc::span(v).last(2);
    CHECK(cr::range(s) == cc::vector{2, 3});

    auto b = cc::span(v).as_bytes();
    CHECK(b.size() == 3 * sizeof(int));

    auto wb = cc::span(v).as_writable_bytes();
    CHECK(wb.size() == 3 * sizeof(int));
    wb[3] = cc::byte(8);
    CHECK(v[0] == 1 + (8 << 24));
}