#include <nexus/app.hh>

#include <phantasm-renderer/pr.hh>

#include "demo_renderer.hh"

namespace
{
void run_demo_renderer(pr::backend_type type)
{
    inc::da::SDLWindow window;
    dr::DemoRenderer renderer(window, type);

    auto const mesh_ball = renderer.loadMesh("res/arcana-sample-resources/phi/mesh/ball.mesh", true);
    auto const mat_ball = renderer.loadMaterial("res/arcana-sample-resources/phi/texture/ball/albedo.png",   //
                                                "res/arcana-sample-resources/phi/texture/ball/normal.png",   //
                                                "res/arcana-sample-resources/phi/texture/ball/metallic.png", //
                                                "res/arcana-sample-resources/phi/texture/ball/roughness.png");

    auto const num_instances = 5u;

    for (auto i = 0u; i < num_instances; ++i)
    {
        auto const modelmat = tg::translation(tg::pos3((-1 + int(i)) * 3.f, 0, 0)) * tg::scaling(0.21f, 0.21f, 0.21f);
        renderer.addInstance(mesh_ball, mat_ball, modelmat);
    }
    renderer.mainLoop([&](pr::Context& ctx, float dt) { renderer.execute(dt); });
}

}

APP("demo_renderer_vk") { run_demo_renderer(pr::backend_type::vulkan); }
APP("demo_renderer_d3d12") { run_demo_renderer(pr::backend_type::d3d12); }
