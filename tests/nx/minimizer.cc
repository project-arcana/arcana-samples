#include <nexus/monte_carlo_test.hh>

#include <clean-core/vector.hh>
#include <clean-ranges/algorithms/contains.hh>


MONTE_CARLO_TEST("mct minimize", disabled)
{
    addValue("empty", cc::vector<int>());
    addOp("add", [](cc::vector<int>& v, tg::rng& rng) { v.push_back(uniform(rng, -10, 10)); });
    addInvariant("tricky", [](cc::vector<int> const& v) { return v.size() < 3 || !cr::contains(v, 7); });

// TODO: good design for lazy minimizer range
//       maybe minimizer is function T -> range T
#if 0
    addMinimizer([this](cc::vector<int> const& val, auto& emitF) {
        // reduce vector size
        for (size_t i = 0; i < val.size(); ++i)
        {
            auto copy = cc::vector<int>::defaulted(val.size() - 1);

            for (size_t j = 0; j < val.size(); ++j)
            {
                if (i == j)
                    continue;
                copy[j < i ? j : j - 1] = val[i];
            }

            if (emitF(copy))
                return;
        }

        // minimize values
        {
            cc::vector<int> copy = val;
            for (size_t i = 0; i < val.size(); ++i)
                for (auto v : minimize(val[i]))
                {
                    copy[i] = v;
                    if (emitF(copy))
                        return;
                }
        }
    });
#endif
}
