#include <nexus/test.hh>

#include <array>
#include <typeindex>
#include <typeinfo>

#include <clean-core/array.hh>

#include <clean-core/string.hh>
#include <clean-core/unique_function.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

TEST("cc::array")
{
    cc::array<int> a;
    CHECK(a.empty());

    a = {1, 2, 3};
    CHECK(a.size() == 3);

    CHECK(tg::sum(a) == 6);

    cc::array<int> b = a;
    CHECK(a == b);

    b[1] = 7;
    CHECK(a != b);

    b = std::move(a);
    CHECK(a.empty());
    CHECK(tg::sum(b) == 6);
}

TEST("fixed cc::array")
{
    cc::array<int, 3> a;

    a = cc::make_array(1, 2, 3);
    CHECK(tg::sum(a) == 6);
}

namespace
{
class AbstractDataType
{
public:
    struct operation
    {
        cc::string name;
        cc::unique_function<void()> fun;
        bool is_supported = true;

        operation& supported_if(bool v)
        {
            is_supported = v;
            return *this;
        }
    };

    // TODO: setSeed

    virtual ~AbstractDataType() = default;

    virtual void checkInvariants() {}

    virtual void step() = 0;

    template <class F>
    operation& op(cc::string name, F&& f)
    {
        auto& op = mOperations.emplace_back();
        op.name = cc::move(name);
        op.fun = cc::forward<F>(f);
        return op;
    }

private:
    cc::vector<operation> mOperations;
};

template <class array_t>
class ArrayTester : public AbstractDataType
{
    tg::rng rng;
    array_t value;

    auto random_value() { return uniform(rng, -10, 10); }

    void step() override
    {
        op("set element", [this] { random_choice(rng, value) = random_value(); });

        op("randomize", [this] {
            for (auto& v : value)
                v = random_value();
        });

        op("def ctor", [this] { value = {}; });
    }
};

class MonteCarloTest
{
public:
    struct function
    {
        cc::string name;
        cc::unique_function<void()> execute;
        cc::vector<std::type_index> arg_types;
        std::type_index return_type;
        bool is_invariant = false;

        int arity() const { return arg_types.size(); }

        template <class F, class R, class... Args>
        function(cc::string name, F&& f, R (F::*op)(Args...)) : name(cc::move(name)), return_type(typeid(R))
        {
            (arg_types.emplace_back(typeid(Args)), ...);
            // TODO: execute!
        }

        template <class R, class... Args>
        function(cc::string name, R (*f)(Args...)) : name(cc::move(name)), return_type(typeid(R))
        {
            (arg_types.emplace_back(typeid(Args)), ...);
            // TODO: execute!
        }
    };

    template <class R, class... Args>
    function& add(cc::string name, R (*f)(Args...))
    {
        mFunctions.emplace_back(cc::move(name), f);
        return mFunctions.back();
    }
    template <class F>
    function& add(cc::string name, F&& f)
    {
        mFunctions.emplace_back(cc::move(name), cc::forward<F>(f));
        return mFunctions.back();
    }

    template <class R, class... Args>
    function& invariant(cc::string name, R (*f)(Args...))
    {
        auto& fun = add(name, f);
        fun.is_invariant = true;
        return fun;
    }
    template <class F>
    function& invariant(cc::string name, F&& f)
    {
        auto& fun = add(name, cc::forward<F>(f));
        fun.is_invariant = true;
        return fun;
    }

private:
    cc::vector<function> mFunctions;
    cc::vector<function> mInvariants;
};
}

TEST("array ADT test")
{
    ArrayTester<cc::array<int, 10>> a0;
    ArrayTester<std::array<int, 10>> a1;

    MonteCarloTest mct;
    mct.add("gen", [](tg::rng& rng) { return uniform(rng, -10, 10) * 2; });
    mct.add("add", [](int a, int b) { return a + b; });
    mct.add("sub", [](int a, int b) { return a - b; });
    mct.invariant("mod 2", [](int i) { CHECK(i % 2 == 0); });
}
