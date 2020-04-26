#include <nexus/app.hh>

#include <resource-system/res.hh>

#include <phantasm-renderer/pr.hh>

#include <structured-interface/gui.hh>
#include <structured-interface/si.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>

namespace
{
constexpr auto shader_code_clear = R"(
                              struct vs_in
                              {
                                  uint vid            : SV_VertexID;
                              };

                              struct vs_out
                              {
                                  float4 SV_P         : SV_POSITION;
                                  float2 Texcoord     : TEXCOORD;
                              };

                              vs_out main_vs(vs_in In)
                              {
                                  vs_out Out;
                                  Out.Texcoord = float2((In.vid << 1) & 2, In.vid & 2);
                                  Out.SV_P = float4(Out.Texcoord * 2.0f + -1.0f, 0.0f, 1.0f);
                                  Out.SV_P.y = -Out.SV_P.y;
                                  return Out;
                              }

                              float4 main_ps(vs_out In) : SV_TARGET
                              {
                                  return float4(1, 0, 1, 1);
                              }
                          )";
}

APP("ui rendering")
{
    // TODO: single line versions
    inc::da::SDLWindow window;
    window.initialize("structured interface");

    pr::Context ctx;
    ctx.initialize({window.getSdlWindow()}, pr::backend::vulkan);

    auto vs_clear = ctx.make_shader(shader_code_clear, "main_vs", pr::shader::vertex);
    auto ps_clear = ctx.make_shader(shader_code_clear, "main_ps", pr::shader::pixel);

    while (!window.isRequestingClose())
    {
        window.pollEvents();

        auto backbuffer = ctx.acquire_backbuffer();
        auto frame = ctx.make_frame();

        {
            auto fb = frame.make_framebuffer(backbuffer);
            auto pass = fb.make_pass(pr::graphics_pass({}, vs_clear, ps_clear));
            pass.draw(3);
        }

        frame.present_after_submit(backbuffer);
        ctx.submit(cc::move(frame));
    }
}
