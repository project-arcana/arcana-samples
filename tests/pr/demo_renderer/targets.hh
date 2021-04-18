#pragma once

#include <typed-geometry/types/size.hh>

#include <phantasm-renderer/resource_types.hh>

namespace dmr
{
struct global_targets
{
    //
    // RTs
    pr::texture t_depth;
    pr::texture t_forward_hdr;
    pr::texture t_forward_velocity;

    pr::texture t_history_a;
    pr::texture t_history_b;

    void recreate_rts(pr::Context& ctx, tg::isize2 new_size);
    void free_rts(pr::Context& ctx);
};
}
