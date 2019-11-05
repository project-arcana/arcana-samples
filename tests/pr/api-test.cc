#include <phantasm-renderer/Context.hh>
#include <phantasm-renderer/Frame.hh>

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

void _pr_api()
{
    //

    pr::Context ctx;

    int w = 1;
    int h = 1;

    std::vector<my_vertex> vertices; // = ...

    camera_data data;
    data.proj = tg::mat4(); // ...
    data.view = tg::mat4(); // ...

    instance_data instanceA; // = ...
    instance_data instanceB; // = ...

    {
        auto frame = ctx.make_frame();

        auto vertex_buffer = frame.make_buffer<my_vertex>(vertices);

        auto t_depth = frame.make_image({w, h}, 0.0f);
        auto t_color = frame.make_image({w, h}, tg::color3::black);

        auto fshader = frame.make_fragment_shader<tg::color3>("<CODE>");
        auto vshader = frame.make_vertex_shader<my_vertex>("<CODE>");

        {
            // trying to start another pass while this one is active is a CONTRACT VIOLATION
            // by default sets viewport to min over image sizes
            auto fb = frame.render_to(t_color, t_depth).bind(data);

            // framebuffer + shader + config = pass
            auto pass = fb.pipeline<instance_data>(vshader, fshader, pr::default_config);

            // issue draw command (explicit bind)
            pass.bind(instanceA).draw(vertex_buffer);

            // issue draw command (variadic draw)
            pass.draw(instanceB, vertex_buffer);
        }

        // submit frame
        ctx.submit(frame);
    }
}
