#include <nexus/test.hh>

#include <arcana-incubator/framegraph/framegraph.hh>

namespace
{
enum E_ResourceGuids : uint64_t
{
    E_RESGUID_MAIN_DEPTH,
    E_RESGUID_VELOCITY,
    E_RESGUID_SCENE,
    E_RESGUID_TAA_PREVIOUS,
    E_RESGUID_TAA_CURRENT,
    E_RESGUID_POSTFX_HDR,
    E_RESGUID_TONEMAP_OUT,
    E_RESGUID_UNUSED_1,
};

}

TEST("framegraph basics")
{
    inc::frag::GraphBuilder builder(50, 50);

    struct depthpre_data
    {
        inc::frag::virtual_resource_handle depth;
        inc::frag::virtual_resource_handle velocity;
    };

    builder.addPass<depthpre_data>(
        "depth_pre",
        [&](depthpre_data& data, inc::frag::GraphBuilder::setup_context& ctx) {
            data.depth = ctx.create(E_RESGUID_MAIN_DEPTH,
                                    phi::arg::create_resource_info::render_target(phi::format::depth32f, ctx.targetWidth(), ctx.targetHeight()),
                                    phi::resource_state::depth_write);
            data.velocity = ctx.create(E_RESGUID_VELOCITY,
                                       phi::arg::create_resource_info::render_target(phi::format::rg16f, ctx.targetWidth(), ctx.targetHeight()),
                                       phi::resource_state::render_target);
        },
        [=](depthpre_data const& data, inc::frag::GraphBuilder::execute_context& ctx) {
            auto const depth = ctx.get(data.depth).as_target();
            auto const raw_velocity = ctx.get(data.velocity).as_target();
        });

    struct forward_data
    {
        inc::frag::virtual_resource_handle depth;
        inc::frag::virtual_resource_handle vel;
        inc::frag::virtual_resource_handle scene;
    };

    builder.addPass<forward_data>(
        "main_forward",
        [&](forward_data& data, inc::frag::GraphBuilder::setup_context& ctx) {
            data.depth = ctx.readWrite(E_RESGUID_MAIN_DEPTH);
            data.vel = ctx.readWrite(E_RESGUID_VELOCITY);
            data.scene = ctx.create(E_RESGUID_SCENE, {});
        },
        [=](forward_data const& data, inc::frag::GraphBuilder::execute_context& ctx) {

        });

    struct taa_data
    {
        inc::frag::virtual_resource_handle depth;
        inc::frag::virtual_resource_handle scene;
        inc::frag::virtual_resource_handle vel;
        inc::frag::virtual_resource_handle prev;
        inc::frag::virtual_resource_handle curr;
    };

    builder.addPass<taa_data>(
        "taa_resolve",
        [&](taa_data& data, inc::frag::GraphBuilder::setup_context& ctx) {
            data.depth = ctx.read(E_RESGUID_MAIN_DEPTH);
            data.scene = ctx.read(E_RESGUID_SCENE);
            data.vel = ctx.read(E_RESGUID_VELOCITY);
            data.prev = ctx.import(E_RESGUID_TAA_PREVIOUS, pr::raw_resource{});
            data.curr = ctx.import(E_RESGUID_TAA_CURRENT, pr::raw_resource{});
            ctx.move(E_RESGUID_TAA_CURRENT, E_RESGUID_POSTFX_HDR);
        },
        [=](taa_data const& data, inc::frag::GraphBuilder::execute_context& ctx) {

        });

    struct tonemap_data
    {
        inc::frag::virtual_resource_handle hdr_in;
        inc::frag::virtual_resource_handle hdr_out;
    };

    builder.addPass<tonemap_data>(
        "tonemap_blit",
        [&](tonemap_data& data, inc::frag::GraphBuilder::setup_context& ctx) {
            data.hdr_in = ctx.read(E_RESGUID_POSTFX_HDR);
            data.hdr_out = ctx.create(E_RESGUID_TONEMAP_OUT, {});
            ctx.move(E_RESGUID_TONEMAP_OUT, E_RESGUID_POSTFX_HDR);
            ctx.setRoot();
        },
        [=](tonemap_data const& data, inc::frag::GraphBuilder::execute_context& ctx) {

        });

    builder.cull();
    builder.realizePhysicalResources([&](phi::arg::create_resource_info const& info) {
        // TODO
        return pr::raw_resource{};
    });

    builder.calculateBarriers();
    builder.execute();
}
