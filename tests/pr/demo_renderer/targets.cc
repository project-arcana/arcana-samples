#include "targets.hh"

#include <phantasm-renderer/Context.hh>

void dmr::global_targets::recreate_rts(pr::Context& ctx, tg::isize2 new_size)
{
    free_rts(ctx);

    constexpr auto hdr_format = pr::format::b10g11r11uf;

    t_depth = ctx.make_target(new_size, pr::format::depth32f, 1, 1, phi::rt_clear_value(0.f, 0)).disown();
    t_forward_hdr = ctx.make_target(new_size, hdr_format).disown();
    t_forward_velocity = ctx.make_target(new_size, pr::format::rg16f, 1, 1, phi::rt_clear_value(1, 1, 1, 1)).disown();

    t_history_a = ctx.make_target(new_size, hdr_format).disown();
    t_history_b = ctx.make_target(new_size, hdr_format).disown();
}

void dmr::global_targets::free_rts(pr::Context& ctx)
{
    // these all default-init to null_resource, thus no initial branch required
    ctx.free_multiple_resources(t_depth, t_forward_hdr, t_forward_velocity, t_history_a, t_history_b);
}
