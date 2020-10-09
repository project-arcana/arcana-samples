#include <nexus/test.hh>

#include <clean-core/array.hh>
#include <clean-core/range_ref.hh>
#include <clean-core/string.hh>
#include <clean-core/string_view.hh>
#include <clean-core/vector.hh>

TEST("cc::range_ref")
{
    auto check_range = [](cc::range_ref<int> r) {
        cc::vector<int> v;
        r.for_each([&](int i) { v.push_back(i); });
        CHECK(v == cc::vector<int>{1, 2, 3});
    };

    {
        cc::vector<int> v = {1, 2, 3};
        check_range(v);
        check_range(cc::make_range_ref(v));
        check_range(cc::make_range_ref<int>(v));
    }
    {
        cc::array<int> v = {1, 2, 3};
        check_range(v);
        check_range(cc::make_range_ref(v));
        check_range(cc::make_range_ref<int>(v));
    }
    {
        check_range({1, 2, 3});
        check_range(cc::make_range_ref({1, 2, 3}));
        check_range(cc::make_range_ref<int>({1, 2, 3}));
    }
}

TEST("cc::range_ref conversion")
{
    auto check_range = [](cc::range_ref<cc::string_view> r, cc::string_view result) {
        cc::string s;
        r.for_each([&](cc::string_view sv) {
            if (!s.empty())
                s += ' ';
            s += sv;
        });
        CHECK(s == result);
    };

    {
        cc::vector<cc::string> words = {"brave", "new", "world"};
        check_range(words, "brave new world");
        check_range(cc::make_range_ref(words), "brave new world");
        check_range(cc::make_range_ref<cc::string_view>(words), "brave new world");
    }
    {
        cc::array<cc::string_view> words = {"brave", "new", "world"};
        check_range(words, "brave new world");
        check_range(cc::make_range_ref(words), "brave new world");
        check_range(cc::make_range_ref<cc::string_view>(words), "brave new world");
    }
    {
        char const* words[] = {"brave", "new", "world"};
        check_range(words, "brave new world");
        check_range(cc::make_range_ref(words), "brave new world");
        check_range(cc::make_range_ref<cc::string_view>(words), "brave new world");
    }
    {
        check_range({"brave", "new", "world"}, "brave new world");
        check_range(cc::make_range_ref({"brave", "new", "world"}), "brave new world");
        check_range(cc::make_range_ref<cc::string_view>({"brave", "new", "world"}), "brave new world");
    }
}
