#include "targets.hh"

#include <phantasm-renderer/Context.hh>

void dmr::global_targets::recreate_rts(pr::Context& ctx, tg::isize2 new_size)
{
    constexpr auto hdr_format = pr::format::b10g11r11uf;

    t_depth = ctx.make_target(new_size, pr::format::depth24un_stencil8u, 0.f);
    t_forward_hdr = ctx.make_target(new_size, hdr_format);
    t_forward_velocity = ctx.make_target(new_size, pr::format::rg16f, tg::color4{1, 1, 1, 1});

    t_history_a = ctx.make_target(new_size, hdr_format);
    t_history_b = ctx.make_target(new_size, hdr_format);

    t_post_a = ctx.make_target(new_size, hdr_format);
    t_post_b = ctx.make_target(new_size, hdr_format);

    auto const halfres = tg::isize2{new_size.width / 2, new_size.height / 2};
    t_post_half_a = ctx.make_target(halfres, hdr_format);
    t_post_half_b = ctx.make_target(halfres, hdr_format);

    t_post_ldr = ctx.make_target(new_size, pr::format::rgba8un);
}

void dmr::global_targets::recreate_buffers(pr::Context& ctx, tg::isize2 new_size)
{
    // TODO
}
