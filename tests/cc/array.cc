#include <nexus/test.hh>

#include <array>
#include <set>
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
class MonteCarloTest
{
public:
    struct value
    {
        using deleter_t = void (*)(void*);

        void* ptr = nullptr;
        deleter_t deleter = nullptr;
        std::type_index type;

        template <class T>
        value(T* v) : ptr(v), deleter([](void* p) { delete static_cast<T*>(p); }), type(typeid(T))
        {
        }
        value(value&& rhs) noexcept : type(rhs.type)
        {
            ptr = rhs.ptr;
            deleter = rhs.deleter;
            rhs.ptr = nullptr;
            rhs.deleter = nullptr;
        }
        value& operator=(value&& rhs) noexcept
        {
            if (ptr)
                deleter(ptr);

            ptr = rhs.ptr;
            deleter = rhs.deleter;
            type = rhs.type;
            rhs.ptr = nullptr;
            rhs.deleter = nullptr;

            return *this;
        }
        ~value()
        {
            if (ptr)
                deleter(ptr);
        }
    };

    struct constant
    {
        cc::string name;
        value val;
    };

    struct machine
    {
    };

    struct function
    {
        // TODO: logging/printing inputs -> outputs

        cc::string name;
        cc::unique_function<void()> execute;
        cc::vector<std::type_index> arg_types;
        std::type_index return_type;
        bool is_invariant = false;

        int arity() const { return arg_types.size(); }

        template <class F, class R, class... Args>
        function(cc::string name, F&& f, R (F::*op)(Args...)) : name(cc::move(name)), return_type(typeid(std::decay_t<R>))
        {
            (arg_types.emplace_back(typeid(std::decay_t<Args>)), ...);
            // TODO: execute!
        }

        template <class F, class R, class... Args>
        function(cc::string name, F&& f, R (F::*op)(Args...) const) : name(cc::move(name)), return_type(typeid(std::decay_t<R>))
        {
            (arg_types.emplace_back(typeid(std::decay_t<Args>)), ...);
            // TODO: execute!
        }

        template <class R, class... Args>
        function(cc::string name, R (*f)(Args...)) : name(cc::move(name)), return_type(typeid(std::decay_t<R>))
        {
            (arg_types.emplace_back(typeid(std::decay_t<Args>)), ...);
            // TODO: execute!
        }
    };

    template <class R, class... Args>
    function& add_op(cc::string name, R (*f)(Args...))
    {
        mFunctions.emplace_back(cc::move(name), f);
        return mFunctions.back();
    }
    template <class F>
    function& add_op(cc::string name, F&& f)
    {
        mFunctions.emplace_back(cc::move(name), cc::forward<F>(f), &F::operator());
        return mFunctions.back();
    }

    template <class F>
    function& add_invariant(cc::string name, F&& f)
    {
        auto& fun = add_op(name, cc::forward<F>(f));
        fun.is_invariant = true;
        return fun;
    }

    template <class T>
    void add_value(cc::string name, T&& v)
    {
        mConstants.push_back({cc::move(name), new T(cc::forward<T>(v))});
    }
    template <class T, class... Args>
    void emplace_value(cc::string name, Args&&... args)
    {
        mConstants.push_back({cc::move(name), new T(cc::forward<Args>(args)...)});
    }

    // TODO: test_substitutability(...)

private:
    std::set<std::type_index> mHasDefaultCtor;
    cc::vector<function> mFunctions;
    cc::vector<constant> mConstants;
};
}

TEST("array ADT test")
{
    MonteCarloTest mct;
    mct.add_value("rng", tg::rng());
    mct.add_op("gen", [](tg::rng& rng) { return uniform(rng, -10, 10) * 2; });
    mct.add_op("add", [](int a, int b) { return a + b; });
    mct.add_op("sub", [](int a, int b) { return a - b; });
    mct.add_invariant("mod 2", [](int i) { CHECK(i % 2 == 0); });
}
