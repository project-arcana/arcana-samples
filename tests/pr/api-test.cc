#include <nexus/test.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>

#include <phantasm-renderer/Context.hh>
#include <phantasm-renderer/Frame.hh>
#include <phantasm-renderer/PrimitivePipeline.hh>
#include <phantasm-renderer/immediate.hh>

namespace
{
struct my_vertex
{
    tg::pos3 pos;
    tg::color3 color;
};
template <class I>
constexpr void introspect(I&& i, my_vertex& v)
{
    i(v.pos, "pos");
    i(v.color, "color");
}

struct camera_data : pr::resources
{
    tg::mat4 proj;
    tg::mat4 view;
};
template <class I>
constexpr void introspect(I&& i, camera_data& v)
{
    i(v.proj, "proj");
    i(v.view, "view");
}

struct instance_data : pr::immediate
{
    tg::mat4 model;
};
template <class I>
constexpr void introspect(I&& i, instance_data& v)
{
    i(v.model, "model");
}


}

TEST("pr::api")
{
    //

    inc::da::SDLWindow window;
    window.initialize("api test");

    pr::Context ctx(phi::window_handle{window.getSdlWindow()});

    int w = 1;
    int h = 1;

    cc::vector<my_vertex> vertices; // = ...
    vertices.emplace_back();

    camera_data data;
    data.proj = tg::mat4(); // ...
    data.view = tg::mat4(); // ...

    instance_data instanceA; // = ...
    instance_data instanceB; // = ...

    auto t_depth = ctx.make_target({w, h}, pr::format::depth32f);
    auto t_color = ctx.make_target({w, h}, pr::format::rgba16f);
    auto vertex_buffer = ctx.make_upload_buffer(sizeof(my_vertex) * vertices.size(), sizeof(my_vertex));

    {
//        auto frame = ctx.make_frame();

//        auto fshader = ctx.make_fragment_shader<pr::format::rgba16f>("<CODE>");
//        auto vshader = ctx.make_vertex_shader<my_vertex>("<CODE>");

//        {
//            // trying to start another pass while this one is active is a CONTRACT VIOLATION
//            // by default sets viewport to min over image sizes
//            auto fb = frame.render_to(t_color, t_depth).bind(data);

//            // framebuffer + shader + config = pass
//            auto pass = fb.pipeline<instance_data>(vshader, fshader, pr::default_config);

//            // issue draw command (explicit bind)
//            pass.bind(instanceA).draw(vertex_buffer);

//            // issue draw command (variadic draw)
//            pass.draw(instanceB, vertex_buffer);
//        }

        // submit frame
//        ctx.submit(frame);
    }
}
