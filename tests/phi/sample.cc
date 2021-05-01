#include "sample.hh"

#include <cstdio>

#include <nexus/args.hh>

phi::backend_config phi_test::get_backend_config()
{
    phi::backend_config config;
    config.adapter = phi::adapter_preference::highest_vram;
    config.num_threads = td::is_scheduler_alive() ? td::get_current_num_threads() : 1;

    config.validation =
#ifdef NDEBUG
        phi::validation_level::off;
#else
        phi::validation_level::on;
#endif

    if (nx::has_cmd_arg("--nsight"))
    {
        config.validation = phi::validation_level::off;
        std::printf("info: launched with --nsight flag, disabling validation\n");
    }

    return config;
}
