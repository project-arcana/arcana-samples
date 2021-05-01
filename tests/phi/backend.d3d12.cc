#ifdef PHI_BACKEND_D3D12
#include <nexus/app.hh>
#include <nexus/test.hh>

#include <iostream>

#include <task-dispatcher/td.hh>

#include "sample.hh"

#include <phantasm-hardware-interface/d3d12/BackendD3D12.hh>
#include <phantasm-hardware-interface/d3d12/adapter_choice_util.hh>

namespace
{
phi_test::sample_config get_d3d12_sample_conf()
{
    phi_test::sample_config sample_conf;
    sample_conf.window_title = "phi::d3d12 liveness";
    sample_conf.shader_ending = "dxil";
    sample_conf.align_mip_rows = true;
    return sample_conf;
}
}

APP("d3d12_pbr")
{
    td::scheduler_config config = {};
    //    config.pin_threads_to_cores = true;
    td::launch(config, [&] {
        phi::d3d12::BackendD3D12 backend;
        phi_test::run_pbr_sample(backend, get_d3d12_sample_conf());
    });
}

APP("d3d12_async_compute")
{
    td::scheduler_config config = {};
    td::launch(config, [&] {
        phi::d3d12::BackendD3D12 backend;
        phi_test::run_nbody_async_compute_sample(backend, get_d3d12_sample_conf());
    });
}

APP("d3d12_rt")
{
    phi::d3d12::BackendD3D12 backend;
    phi_test::run_raytracing_sample(backend, get_d3d12_sample_conf());
}

APP("d3d12_pathtrace")
{
    phi::d3d12::BackendD3D12 backend;
    phi_test::run_pathtracing_sample(backend, get_d3d12_sample_conf());
}

APP("d3d12_imgui")
{
    phi::d3d12::BackendD3D12 backend;
    phi_test::run_imgui_sample(backend, get_d3d12_sample_conf());
}

APP("d3d12_bindless")
{
    phi::d3d12::BackendD3D12 backend;
    phi_test::run_bindless_sample(backend, get_d3d12_sample_conf());
}

#endif
