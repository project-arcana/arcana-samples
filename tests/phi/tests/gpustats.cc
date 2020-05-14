#include <nexus/test.hh>

#include <rich-log/log.hh>

#include <phantasm-hardware-interface/detail/gpu_stats.hh>

TEST("gpustats", disabled)
{
    phi::gpustats::initialize();

    auto const gpu = phi::gpustats::get_gpu_by_index(0);
    LOG("handle for gpu#0: {}", gpu);

    int temp = phi::gpustats::get_temperature(gpu);
    int fan = phi::gpustats::get_fanspeed_percent(gpu);
    LOG("temperature: {}C, fan speed: {}%", temp, fan);

    phi::gpustats::shutdown();
}
