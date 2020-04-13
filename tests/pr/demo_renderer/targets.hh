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

    pr::auto_render_target t_post_a;
    pr::auto_render_target t_post_b;
    pr::auto_render_target t_post_half_a;
    pr::auto_render_target t_post_half_b;

    pr::auto_render_target t_post_ldr;

    //
    // Buffers
    pr::auto_buffer b_cluster_visibilities;
    pr::auto_buffer b_cluster_aabbs;
    pr::auto_buffer b_light_index_list;
    pr::auto_buffer b_global_index_count;
    pr::auto_buffer b_light_array;

    void recreate_rts(pr::Context& ctx, tg::isize2 new_size);

    void recreate_buffers(pr::Context& ctx, tg::isize2 new_size);
};
}
