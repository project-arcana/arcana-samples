#include <nexus/test.hh>

#include <clean-core/poly_unique_ptr.hh>
#include <clean-core/vector.hh>

#include <reflector/enums.hh>
#include <reflector/introspect.hh>
#include <reflector/macros.hh>

namespace
{
enum class foo
{
    val0,
    val1,
    val2
};

template <class In>
constexpr void introspect_enum(In&& inspect, foo& f)
{
    inspect(f, foo::val0, "val0");
    inspect(f, foo::val1, "val1");
    inspect(f, foo::val2, "val2");
}
}

TEST("enum")
{
    static_assert(rf::enum_value_count<foo> == 3);

    static_assert(rf::enum_values<foo>[0] == foo::val0);
    static_assert(rf::enum_values<foo>[1] == foo::val1);
    static_assert(rf::enum_values<foo>[2] == foo::val2);

    static_assert(rf::enum_names<foo>[0] == "val0");
    static_assert(rf::enum_names<foo>[1] == "val1");
    static_assert(rf::enum_names<foo>[2] == "val2");

    static_assert(rf::enum_to_string(foo::val0) == "val0");
    static_assert(rf::enum_to_string(foo::val1) == "val1");
    static_assert(rf::enum_to_string(foo::val2) == "val2");

    static_assert(rf::enum_from_string<foo>("val0") == foo::val0);
    static_assert(rf::enum_from_string<foo>("val1") == foo::val1);
    static_assert(rf::enum_from_string<foo>("val2") == foo::val2);

    static_assert(rf::is_enum_value_valid(foo::val0));
    static_assert(!rf::is_enum_value_valid(foo(10)));

    CHECK(true);
}

namespace
{
struct foo_base_val
{
    virtual cc::string_view test() const = 0;
    virtual ~foo_base_val() = default;
};

struct foo_val0 : foo_base_val
{
    cc::string_view test() const override { return "foo for val0"; }
};
struct foo_val1 : foo_base_val
{
    cc::string_view test() const override { return "foo for val1"; }
};
struct foo_val2 : foo_base_val
{
    cc::string_view test() const override { return "foo for val2"; }
};

template <foo f>
struct type_for_foo_t;

template <>
struct type_for_foo_t<foo::val0>
{
    using type = foo_val0;
};
template <>
struct type_for_foo_t<foo::val1>
{
    using type = foo_val1;
};
template <>
struct type_for_foo_t<foo::val2>
{
    using type = foo_val2;
};

template <foo f>
using type_for_foo = typename type_for_foo_t<f>::type;
}

TEST("enum invoke")
{
    cc::vector<cc::poly_unique_ptr<foo_base_val>> vals;

    for (auto f : {foo::val2, foo::val0, foo::val1, foo::val2})
        vals.push_back(rf::enum_invoke(f, [](auto v) -> cc::poly_unique_ptr<foo_base_val> {
            using type_t = type_for_foo<v.value>;
            return cc::make_poly_unique<type_t>();
        }));

    CHECK(vals.size() == 4);
    CHECK(vals[0]->test() == "foo for val2");
    CHECK(vals[1]->test() == "foo for val0");
    CHECK(vals[2]->test() == "foo for val1");
    CHECK(vals[3]->test() == "foo for val2");
}
