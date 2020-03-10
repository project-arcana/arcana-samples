#include <nexus/test.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>

#include <phantasm-renderer/Context.hh>
#include <phantasm-renderer/Frame.hh>
#include <phantasm-renderer/PrimitivePipeline.hh>

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

#if 0
TEST("pr::api")
{
    //

    inc::da::SDLWindow window;
    window.initialize("api test");

    auto ctx = pr::Context(phi::window_handle{window.getSdlWindow()});

    int w = 1;
    int h = 1;

    std::vector<my_vertex> vertices; // = ...
    vertices.emplace_back();

    camera_data data;
    data.proj = tg::mat4(); // ...
    data.view = tg::mat4(); // ...

    instance_data instanceA; // = ...
    instance_data instanceB; // = ...

    auto t_depth = ctx.make_target<pr::format::depth32f>({w, h}, 0.f);
    auto t_color = ctx.make_target<pr::format::rgba16f>({w, h}, tg::color3::black);
    auto vertex_buffer = ctx.make_upload_buffer<my_vertex>(vertices);

    {
        auto frame = ctx.make_frame();

        auto fshader = ctx.make_fragment_shader<pr::format::rgba16f>("<CODE>");
        auto vshader = ctx.make_vertex_shader<my_vertex>("<CODE>");

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

    // bind versions
    {
        auto pass = ...;

        struct my_data
        {
            int idx;
            tg::color4 color;
        };

        // trivially copyable T
        {
            tg::mat4 transform = ...;
            pass.bind(transform).draw(...);

            my_data data = ...;
            pass.bind(data).draw(...);
        }

        // contiguous range of trivially copyable T
        {
            cc::vector<tg::mat4> transforms = ...;
            pass.bind(transforms).draw(...);

            cc::array<my_data> data = ...;
            pass.bind(data).draw(...);
        }

        // custom setup
        {
            struct my_arg : pr::shader_arg
            {
                tg::mat4 model;
                pr::ImageView2D tex_albedo;
                pr::ImageView2D tex_normal;

                // alternatively also via reflection
                void setup()
                {
                    add(model);
                    add(tex_albedo);
                    add(tex_normal);
                }
            };

            my_arg arg = ...;
            pass.bind(arg).draw(...);
        }

        // directly some resources
        {
            pr::Buffer buffer = ...;
            pass.bind(buffer).draw(...);

            cc::vector<pr::Buffer> buffers = ...;
            pass.bind(buffers).draw(...);

            pr::Image2D tex = ...;
            pass.bind(tex).draw(...);

            cc::vector<pr::Image2D> textures = ...;
            pass.bind(textures).draw(...);

            pr::Image2D texA, texB, texC = ...;
            pass.bind({texA, texB, texC}).draw(...);
        }

        // shader arg builder
        {
            pr::Buffer buffer = ...;
            cc::vector<pr::Buffer> buffers = ...;
            pr::Image2D tex = ...;
            cc::vector<pr::Image2D> textures = ...;
            my_data data = ...;
            tg::mat4 transform = ...;

            pr::shader_arg arg;
            arg.add(buffer);
            arg.add(buffers);
            arg.add(tex, "my_tex"); // name is optional for verification
            arg.add(textures);
            arg.add(data);
            arg.add(transform);
            pass.bind(arg).draw(...);
        }
    }
}
#endif
