#include <doctest.hh>

#include <iostream>

#include <phantasm-renderer/backend/d3d12/Adapter.hh>

TEST_CASE("pr backend liveness")
{
#ifdef PR_BACKEND_D3D12
    {
        pr::backend::d3d12::Adapter adapter;
        adapter.initialize();


        std::cout << "adapter alive!" << std::endl;

        if (adapter.getCapabilities().has_sm6_wave_intrinsics)
            std::cout << "Adapter has SM6 Wave intrinsics" << std::endl;

        if (adapter.getCapabilities().has_raytracing)
            std::cout << "Adapter has Raytracing" << std::endl;
    }
#endif
}
