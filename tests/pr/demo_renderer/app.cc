#include <nexus/app.hh>

#include <phantasm-renderer/pr.hh>

#include "demo_renderer.hh"

namespace
{
void run_demo_renderer(pr::backend type)
{
    // init
    inc::da::initialize();
    inc::da::SDLWindow window("Demo Renderer", 1280, 720);
    dmr::DemoRenderer renderer(window, type);

    // assets
    auto const mesh_ball = renderer.loadMesh("res/arcana-sample-resources/phi/mesh/ball.mesh", true);
    auto const mesh_colt = renderer.loadMesh("res/arcana-sample-resources/phi/mesh/colt1911.mesh", true);
    auto const mesh_car = renderer.loadMesh("res/arcana-sample-resources/phi/mesh/old_car.mesh", true);
    auto const mat_ball = renderer.loadMaterial("res/arcana-sample-resources/phi/texture/ball/albedo.png",   //
                                                "res/arcana-sample-resources/phi/texture/ball/normal.png",   //
                                                "res/arcana-sample-resources/phi/texture/ball/metallic.png", //
                                                "res/arcana-sample-resources/phi/texture/ball/roughness.png");
    auto const mat_colt = renderer.loadMaterial("res/arcana-sample-resources/phi/texture/colt/albedo.png",   //
                                                "res/arcana-sample-resources/phi/texture/colt/normal.png",   //
                                                "res/arcana-sample-resources/phi/texture/colt/metallic.png", //
                                                "res/arcana-sample-resources/phi/texture/colt/roughness.png");
    auto const mat_car = renderer.loadMaterial("res/arcana-sample-resources/phi/texture/oldcar/albedo.png",   //
                                               "res/arcana-sample-resources/phi/texture/oldcar/normal.png",   //
                                               "res/arcana-sample-resources/phi/texture/oldcar/metallic.png", //
                                               "res/arcana-sample-resources/phi/texture/oldcar/roughness.png");

    // scene
    auto const add_ball = [&](float x, float y, float z) {
        renderer.addInstance(mesh_ball, mat_ball, tg::translation<float>(x, y, z) * tg::rotation_y(180_deg) * tg::scaling(.21f, .21f, .21f));
    };

    add_ball(-6 * 1.5, 0, 5);
    add_ball(-4 * 1.5, 0, 6);
    add_ball(-2 * 1.5, 0, 7);
    add_ball(0, 0, 8);
    add_ball(2 * 1.5, 0, 7);
    add_ball(4 * 1.5, 0, 6);
    add_ball(6 * 1.5, 0, 5);

    renderer.addInstance(mesh_colt, mat_colt, tg::translation<float>(0, -1, 2) * tg::rotation_y(-90_deg) * tg::scaling(.75f, .75f, .75f));
    renderer.addInstance(mesh_car, mat_car, tg::translation<float>(-7, -1, 14) * tg::rotation_y(35_deg) * tg::rotation_x(5_deg) * tg::scaling(.75f, .75f, .75f));


    // camera
    renderer.camera().target.position = {-10.3f, 4.74f, -2.05f};
    renderer.camera().target.forward = tg::normalize(tg::vec3{0.5f, -0.43f, 0.76f});
    renderer.camera().interpolate_to_target(.1f);

    // run
    renderer.mainLoop([&](pr::Context&, float dt) { renderer.execute(dt); });

    // shutdown
    renderer.destroy();
    window.destroy();
    inc::da::shutdown();
}

}

APP("demo_renderer_vk") { run_demo_renderer(pr::backend::vulkan); }
APP("demo_renderer_d3d12") { run_demo_renderer(pr::backend::d3d12); }
