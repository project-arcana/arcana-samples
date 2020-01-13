#include "sample.hh"

#include <cmath>

#include <rich-log/log.hh>

#include <clean-core/capped_array.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>
#include <clean-core/vector.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-renderer/backend/command_stream.hh>
#include <phantasm-renderer/backend/detail/unique_buffer.hh>

#include <arcana-incubator/device-abstraction/timer.hh>
#include <arcana-incubator/device-abstraction/window.hh>

#include "sample_util.hh"
#include "texture_util.hh"


void pr_test::run_raytracing_sample(pr::backend::Backend& backend, sample_config const& sample_config, pr::backend::backend_config const& backend_config)
{
    using namespace pr::backend;

    inc::da::Window window;
    window.initialize(sample_config.window_title);
    backend.initialize(backend_config, {window.getNativeHandleA(), window.getNativeHandleB()});

    if (!backend.gpuHasRaytracing())
    {
        LOG(warning)("Current GPU has no raytracing capabilities");
        return;
    }

    auto const on_resize_func = [&]() {};

    auto run_time = 0.f;
    auto log_time = 0.f;
    inc::da::Timer timer;
    while (!window.isRequestingClose())
    {
        window.pollEvents();
        if (window.isPendingResize())
        {
            if (!window.isMinimized())
                backend.onResize({window.getWidth(), window.getHeight()});
            window.clearPendingResize();
        }

        if (!window.isMinimized())
        {
            auto const frametime = timer.elapsedMilliseconds();
            timer.restart();
            run_time += frametime / 1000.f;
            log_time += frametime;

            if (log_time >= 1750.f)
            {
                log_time = 0.f;
                LOG(info)("frametime: %fms", static_cast<double>(frametime));
            }

            if (backend.clearPendingResize())
                on_resize_func();

            backend.acquireBackbuffer();

            // present
            backend.present();
        }
    }

    backend.flushGPU();
}
