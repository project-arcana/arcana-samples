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
    inc::frag::GraphBuilder builder;


    auto const p_depthpre = builder.addPass(
        +[](inc::frag::execution_context&, void*) {}, nullptr, "depth_pre");
    builder.registerCreate(p_depthpre, E_RESGUID_MAIN_DEPTH, {}, {});
    builder.registerCreate(p_depthpre, E_RESGUID_VELOCITY, {}, {});

    auto const p_forward = builder.addPass(
        +[](inc::frag::execution_context&, void*) {}, nullptr, "main_forward");
    builder.registerReadWrite(p_forward, E_RESGUID_MAIN_DEPTH, {});
    builder.registerReadWrite(p_forward, E_RESGUID_VELOCITY, {});
    builder.registerCreate(p_forward, E_RESGUID_SCENE, {}, {});

    auto const p_dummy_unused = builder.addPass(
        +[](inc::frag::execution_context&, void*) {}, nullptr, "unused1");
    builder.registerRead(p_dummy_unused, E_RESGUID_VELOCITY, {});
    builder.registerCreate(p_dummy_unused, E_RESGUID_UNUSED_1, {}, {});

    auto const p_taa = builder.addPass(
        +[](inc::frag::execution_context&, void*) {}, nullptr, "taa_resolve");
    builder.registerRead(p_taa, E_RESGUID_MAIN_DEPTH, {});
    builder.registerRead(p_taa, E_RESGUID_SCENE, {});
    builder.registerRead(p_taa, E_RESGUID_VELOCITY, {});
    builder.registerImport(p_taa, E_RESGUID_TAA_PREVIOUS, phi::handle::null_resource, {});
    builder.registerImport(p_taa, E_RESGUID_TAA_CURRENT, phi::handle::null_resource, {});
    builder.registerMove(p_taa, E_RESGUID_TAA_CURRENT, E_RESGUID_POSTFX_HDR);

    auto const p_tonemap = builder.addPass(
        +[](inc::frag::execution_context&, void*) {}, nullptr, "tonemap");
    builder.registerRead(p_tonemap, E_RESGUID_POSTFX_HDR, {});
    builder.registerCreate(p_tonemap, E_RESGUID_TONEMAP_OUT, {}, {});
    builder.registerMove(p_tonemap, E_RESGUID_TONEMAP_OUT, E_RESGUID_POSTFX_HDR);

    builder.makeResourceRoot(E_RESGUID_POSTFX_HDR);

    builder.compile();
}
