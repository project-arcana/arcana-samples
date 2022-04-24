#include "sample.hh"

#include <cstdio>

#include <nexus/args.hh>

phi::backend_config phi_test::get_backend_config()
{
    phi::backend_config config;
    config.adapter = phi::adapter_preference::highest_vram;
    config.num_threads = td::is_scheduler_alive() ? td::get_current_num_threads() : 1;

    config.validation = phi::validation_level::on;
    config.enable_raytracing = true;

    if (nx::has_cmd_arg("--gbv"))
    {
        config.validation = phi::validation_level::on_extended;
        std::printf("info: launched with --gbv flag, enabling GPU based validation\n");
    }

    if (nx::has_cmd_arg("--vk_bestpractices"))
    {
        config.native_features |= phi::backend_config::native_feature_vk_best_practices_layer;
        std::printf("info: launched with --vk_bestpractices flag, enabling Vulkan best practices layer\n");
    }

    if (nx::has_cmd_arg("--novalidate"))
    {
        config.validation = phi::validation_level::off;
        std::printf("info: launched with --novalidate flag, disabling validation\n");
    }

    return config;
}
