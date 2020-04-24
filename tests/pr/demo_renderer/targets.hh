#pragma once

#include <typed-geometry/types/size.hh>

#include <phantasm-renderer/resource_types.hh>

namespace dmr
{
struct global_targets
{
    //
    // RTs
    pr::auto_render_target t_depth;
    pr::auto_render_target t_forward_hdr;
    pr::auto_render_target t_forward_velocity;

    pr::auto_render_target t_history_a;
    pr::auto_render_target t_history_b;

    //
    // Buffers
    pr::auto_render_target t_cluster_shell_sphere;
    pr::auto_render_target t_cluster_shell_spot;

    void recreate_rts(pr::Context& ctx, tg::isize2 new_size);

    void recreate_buffers(pr::Context& ctx, tg::isize2 new_size);
};
}
