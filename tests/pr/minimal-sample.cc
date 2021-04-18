#include <nexus/app.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>

#include <phantasm-renderer/CompiledFrame.hh>
#include <phantasm-renderer/Context.hh>
#include <phantasm-renderer/Frame.hh>

APP("min_sample")
{
    // create the context
    auto ctx = pr::Context(pr::backend::vulkan);

    // create a window (can be any SDL / Win32 / xlib window)
    inc::da::SDLWindow window;
    window.initialize("phantasm-renderer minimal sample");

    // create a swapchain on the window
    pr::auto_swapchain swapchain = ctx.make_swapchain(phi::window_handle{window.getSdlWindow()}, window.getSize());

    while (!window.isRequestingClose())
    {
        // usual window event loop
        window.pollEvents();
        if (window.isMinimized())
            continue;

        // call pr::Context::on_window_resize if the window resized
        if (window.clearPendingResize())
            ctx.on_window_resize(swapchain, window.getSize());

        if (ctx.clear_backbuffer_resize(swapchain))
        {
            // rendertarget resizes should only ever be based on these two methods
            // both when, and what size
            tg::isize2 new_backbuf_size = ctx.get_backbuffer_size(swapchain);
            (void)new_backbuf_size;
        }

        {
            // create a pr::raii::Frame - a software command buffer
            // (many pr::raii::Frames can be used within a single application frame, or outlive it as well)
            auto frame = ctx.make_frame();

            // operations on frames are recorded, not immediately executed on GPU


            // to present an image to the swapchain (and thus window),
            // a backbuffer must first be acquired, rendered/copied/written to, and transitioned into "present"-state

            // acquiring a backbuffer is NOT a GPU-timeline operation,
            // it is effectively a CPU Fence wait and can stall for non-vsynced swapchains (as the CPU overtakes the presentation)
            pr::texture backbuffer = ctx.acquire_backbuffer(swapchain);

            // this backbuffer can be used like any other render target (except freed)
            // here it's just transitioned to present state
            frame.transition(backbuffer, phi::resource_state::present);

            // before submitting a frame to be executed by the GPU, it must be "compiled"
            // this translates the intermediate format to record a native D3D12/Vulkan commandlist, which is relatively expensive
            // usually you would have N threads recording independent frames, and then very few (1-3) central submits of compiled frames on the main thread
            pr::CompiledFrame comp_frame = ctx.compile(cc::move(frame));

            // submitting a compiled frame enqueues the workload contained in a frame on the GPU work queue,
            // which will work through submitted frames sequentially
            // this call thus returns (way) before the work is done or even started on GPU
            ctx.submit(cc::move(comp_frame));
        }

        // presenting on a swapchain makes the most recently acquired backbuffer visible
        // the synchronization mode of the swapchain determines how this call behaves - vsynced swapchains will stall for milliseconds
        ctx.present(swapchain);
    }

    // (control has now left the main loop above, shutting down)
    // a short note about RAII and lifetimes in pr:
    {
        // auto_ types must be explicitly "disowned" and then manually managed:

        pr::buffer buf2_manual; // (POD, nothing happened here, just a variable)
        {
            pr::auto_buffer buf2 = ctx.make_buffer(64);
            buf2_manual = buf2.disown();
        } // buf2 ran out of scope, but nothing happened

        // buf2_manual must be freed explicitly:
        ctx.free(buf2_manual);
    }

    // note how the swapchain created above is also an auto_ type, it will get destroyed at the end of this sample.
    // resources which might still be in flight on the GPU (referenced in pending frames etc.) must not be destroyed.
    // the easiest and most brutal way of achieving this is "flushing" the GPU - stalling on CPU until all pending operations are completed:
    ctx.flush();
    // now the swapchain can safely run out of scope

    // in a real setting it's catastrophic to flush during normal operation (ie. gameplay)
    // (reasonable times to flush could be setup, teardown, window resizes, level loads, quality setting changes, etc.)
    // for better ways to free resources without violating in-flight constraints, see pr::Context::free_deferred and pr::raii::Frame::free_deferred_after_submit
}
