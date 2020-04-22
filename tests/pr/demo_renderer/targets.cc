#include "targets.hh"

#include <phantasm-renderer/Context.hh>

void dmr::global_targets::recreate_rts(pr::Context& ctx, tg::isize2 new_size)
{
    constexpr auto hdr_format = pr::format::b10g11r11uf;

    t_depth = ctx.make_target(new_size, pr::format::depth32f, 1, 1, phi::rt_clear_value(0.f, 0));
    t_forward_hdr = ctx.make_target(new_size, hdr_format);
    t_forward_velocity = ctx.make_target(new_size, pr::format::rg16f, 1, 1, phi::rt_clear_value(1, 1, 1, 1));

    t_history_a = ctx.make_target(new_size, hdr_format);
    t_history_b = ctx.make_target(new_size, hdr_format);
}

void dmr::global_targets::recreate_buffers(pr::Context& ctx, tg::isize2 new_size)
{
    constexpr auto tile_size = tg::isize2{48, 24};
    constexpr auto num_z_slices = 128;

    t_cluster_shell_sphere = ctx.make_target(tile_size, pr::format::rg8un, 1, num_z_slices);
    t_cluster_shell_spot = ctx.make_target(tile_size, pr::format::rg8un, 1, num_z_slices);
}
