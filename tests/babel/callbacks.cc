#include <nexus/test.hh>

#include <clean-core/vector.hh>

#include <babel-serializer/callback.hh>

TEST("babel::callback")
{
    auto process = [](babel::callback<int> cb, cc::vector<int> const& vals) {
        for (auto v : vals)
            if (cb(v) == babel::callback_behavior::break_)
                return;
    };

    {
        cc::vector<int> v;
        process([&](int i) { v.push_back(i); }, {1, 2, 3});
        CHECK(v == cc::vector<int>{1, 2, 3});
    }
    {
        cc::vector<int> v;
        process(
            [&](int i) {
                v.push_back(i);
                return babel::callback_behavior::break_;
            },
            {1, 2, 3});
        CHECK(v == cc::vector<int>{1});
    }
    {
        cc::vector<int> v;
        process(
            [&](int i) {
                v.push_back(i);
                return i >= 2 ? babel::callback_behavior::break_ : babel::callback_behavior::continue_;
            },
            {1, 2, 3});
        CHECK(v == cc::vector<int>{1, 2});
    }
}
