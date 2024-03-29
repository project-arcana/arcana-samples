#include <nexus/app.hh>

#include <chrono>

#include <resource-system/res.hh>

#include <phantasm-hardware-interface/window_handle.hh>

#include <phantasm-renderer/pr.hh>

#include <typed-geometry/tg.hh>

#include <clean-core/unique_function.hh>

#include <structured-interface/element_tree.hh>
#include <structured-interface/gui.hh>
#include <structured-interface/layout/aabb_layout.hh>
#include <structured-interface/merger/Default2DMerger.hh>
#include <structured-interface/si.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>
#include <arcana-incubator/device-abstraction/input.hh>

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
                                  return float4(0.8,0.8,0.8, 1);
                              }
                          )";
constexpr auto shader_code_ui = R"(
                              struct vs_in
                              {
                                  float2 pos   : POSITION;
                                  float2 uv    : TEXCOORD;
                                  float4 color : COLOR;
                              };

                              struct vs_out
                              {
                                  float4 SV_P  : SV_POSITION;
                                  float4 color : COLOR;
                                  float2 uv    : TEXCOORD;
                              };

                              struct vert_globals
                              {
                                  float4x4 proj;
                              };

                              ConstantBuffer<vert_globals> g_vert_globals : register(b0, space0);

                              SamplerState g_sampler : register(s0, space0);
                              Texture2D g_texture : register(t0, space0);

                              vs_out main_vs(vs_in input)
                              {
                                  vs_out output;
                                  output.SV_P = mul(g_vert_globals.proj, float4(input.pos.xy, 0.f, 1.f));
                                  output.color = input.color;
                                  output.uv = input.uv;
                                  return output;
                              }

                              float4 main_ps(vs_out input) : SV_TARGET
                              {
                                  return input.color * float4(1,1,1,g_texture.Sample(g_sampler, input.uv).x);
                              }
                          )";
constexpr auto shader_code_ui_wireframe = R"(
                              struct vs_in
                              {
                                  float2 pos   : POSITION;
                                  float2 uv    : TEXCOORD;
                                  float4 color : COLOR;
                              };

                              struct vert_globals
                              {
                                  float4x4 proj;
                              };

                              ConstantBuffer<vert_globals> g_vert_globals : register(b0, space0);

                              float4 main_vs(vs_in input) : SV_POSITION
                              {
                                  return mul(g_vert_globals.proj, float4(input.pos.xy, 0.f, 1.f));
                              }

                              float4 main_ps() : SV_TARGET
                              {
                                  return float4(1,0,0,1);
                              }
                          )";
}

APP("ui rendering")
{
    auto window = inc::da::SDLWindow("structured interface");
    auto ctx = pr::Context(pr::backend::vulkan);
    auto swapchain = ctx.make_swapchain(phi::window_handle{window.getSdlWindow()}, window.getSize());

    auto vs_clear = ctx.make_shader(shader_code_clear, "main_vs", pr::shader::vertex);
    auto ps_clear = ctx.make_shader(shader_code_clear, "main_ps", pr::shader::pixel);
    auto vs_ui = ctx.make_shader(shader_code_ui, "main_vs", pr::shader::vertex);
    auto ps_ui = ctx.make_shader(shader_code_ui, "main_ps", pr::shader::pixel);
    auto vs_ui_wireframe = ctx.make_shader(shader_code_ui_wireframe, "main_vs", pr::shader::vertex);
    auto ps_ui_wireframe = ctx.make_shader(shader_code_ui_wireframe, "main_ps", pr::shader::pixel);

    using vertex_t = si::Default2DMerger::vertex;
    phi::vertex_attribute_info vert_attrs[] = {phi::vertex_attribute_info{"POSITION", unsigned(offsetof(vertex_t, pos)), phi::format::rg32f},
                                               phi::vertex_attribute_info{"TEXCOORD", unsigned(offsetof(vertex_t, uv)), phi::format::rg32f},
                                               phi::vertex_attribute_info{"COLOR", unsigned(offsetof(vertex_t, color)), phi::format::rgba8un}};

    auto fb_info = pr::framebuffer_info().target(ctx.get_backbuffer_format(swapchain), pr::blend_state::alpha_blending());
    auto gp_info = pr::graphics_pass(vs_ui, ps_ui).arg(1, 0, 1, true).vertex(sizeof(vertex_t), vert_attrs);
    auto pso_ui = ctx.make_pipeline_state(gp_info, fb_info);

    auto fb_info_wf = pr::framebuffer_info().target(ctx.get_backbuffer_format(swapchain));
    auto gp_info_wf = pr::graphics_pass(vs_ui_wireframe, ps_ui_wireframe).arg(0, 0, 0, true).vertex(sizeof(vertex_t), vert_attrs).wireframe();
    auto pso_ui_wf = ctx.make_pipeline_state(gp_info_wf, fb_info_wf);

    si::gui ui;
    si::Default2DMerger ui_merger;

    // init input
    inc::da::input_manager input;
    input.initialize(100);
    auto is_in_text_edit = false;

    enum e_input : uint64_t
    {
        action_mouse_x = 1000,
        action_mouse_y,
        action_mouse_left,
    };

    input.bindMouseAxis(action_mouse_x, inc::da::mouse_axis::x);
    input.bindMouseAxis(action_mouse_y, inc::da::mouse_axis::y);
    input.bindMouseButton(action_mouse_left, inc::da::mouse_button::mb_left);

    // upload font data
    // TODO: mipmaps
    auto const& font = ui_merger.get_font_atlas();
    auto font_tex = ctx.make_texture({font.width, font.height}, pr::format::r8un, 1);
    auto font_arg = pr::argument();
    font_arg.add(font_tex);
    font_arg.add_sampler(pr::sampler_filter::min_mag_mip_linear, 0, pr::sampler_address_mode::clamp);
    {
        auto fb = ctx.make_upload_buffer(font.data.size());
        CC_ASSERT(int(font.data.size()) == font.width * font.height);
        ctx.write_to_buffer_raw(fb, font.data);

        auto frame = ctx.make_frame();
        frame.copy(fb, font_tex);
        frame.transition(font_tex, pr::state::shader_resource, pr::shader::pixel);
        frame.free_deferred_after_submit(fb.disown());
        ctx.submit(cc::move(frame));
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    auto frame = 0;
    auto clicks = 0;
    auto slider_vali = 0;
    auto slider_valf = 0.f;
    bool use_frame_counter = false;
    bool show_wireframe = false;
    // tg::color3 color = tg::color3::red;
    cc::string my_string = "editable text";

    while (!window.isRequestingClose())
    {
        // text edit state
        if (!is_in_text_edit && ui_merger.is_in_text_edit())
        {
            SDL_StartTextInput();
            is_in_text_edit = true;
        }
        if (is_in_text_edit && !ui_merger.is_in_text_edit())
        {
            SDL_StopTextInput();
            is_in_text_edit = false;
        }

        // input and polling
        {
            input.updatePrePoll();

            SDL_Event e;
            while (window.pollSingleEvent(e))
            {
                // DEBUG: for now!

                if (is_in_text_edit)
                {
                    auto& txt = ui_merger.editable_text();
                    switch (e.type)
                    {
                    case SDL_KEYDOWN:
                        // TODO: ctrl
                        switch (e.key.keysym.scancode)
                        {
                        case SDL_Scancode::SDL_SCANCODE_BACKSPACE:
                            txt.remove_prev_char();
                            break;
                        case SDL_Scancode::SDL_SCANCODE_DELETE:
                            txt.remove_next_char();
                            break;
                        case SDL_Scancode::SDL_SCANCODE_LEFT:
                            txt.move_cursor_left();
                            break;
                        case SDL_Scancode::SDL_SCANCODE_RIGHT:
                            txt.move_cursor_right();
                            break;

                        default:
                            break;
                        }
                        break;

                    case SDL_TEXTINPUT:
                        txt.on_text_input(e.text.text);
                        break;

                        // TODO: TEXTEDITING with composition
                    }
                }

                input.processEvent(e);
            }

            input.updatePostPoll();
        }

        if (use_frame_counter)
            ++frame;

        auto mouse = input.getMousePositionRelative();

        // record ui
        auto t0 = std::chrono::high_resolution_clock::now();
        auto r = ui.record([&] {
            if (si::button("press me"))
                ++clicks;
            si::text("i'm a test text.");
            si::text("frame: {}", frame);
            si::text("clicks: {}", clicks);
            si::text("slider_vals: {} and {}", slider_vali, slider_valf);
            si::text("mouse: {}, {}", mouse.x, mouse.y);
            // si::input("color input", color);

            if (auto r = si::row())
            {
                si::text("same");
                si::button("btn");
                si::text("row");
            }

            if (auto w = si::window("test window"))
            {
                si::text("i'm in a test window!");
                si::slider("with another slider", slider_vali, -20, 100);
            }

            if (auto w = si::window("window 2"))
            {
                si::text("2nd window");
                si::text("for order tests");

                auto t = si::text("i haz tooltip");
                if (auto tt = si::tooltip(si::placement::centered_above()))
                {
                    si::text("with more text");
                    si::text(".. that changes? {}", mouse.x);
                }
            }

            si::checkbox("frame counter (checkbox)", use_frame_counter);
            si::toggle("frame counter (toggle)", use_frame_counter);
            si::checkbox("wireframe", show_wireframe);
            si::slider("int slider", slider_vali, -10, 10);
            si::slider("float slider", slider_valf, -10.f, 10.f);
            si::text("I have a tooltip").tooltip("it's a me, a tooltip!");
            si::textbox("some text", my_string).popover([&] { si::value("my_string", my_string); });

            {
                auto t = si::text("I have a custom tooltip");
                if (auto tt = si::tooltip(si::placement::centered_above()))
                {
                    si::text("with more text");
                    si::text(".. that changes? {}", mouse.x);
                }
            }

            si::text("Custom tooltip via lambda").tooltip([&] { si::text("custom text"); });

            si::text("popover test").popover("I'm a popover!");

            if (auto w = si::window("tooltips"))
            {
                si::text("on text").tooltip("I'm a text tooltip!");
                si::button("on button").tooltip("I'm a button tooltip!");
                si::text("placements:");
                si::button("[default]").tooltip("I'm a tooltip!", si::placement::tooltip_default());
                si::button("[centered_above]").tooltip("I'm a tooltip!", si::placement::centered_above());
                si::button("[centered_below]").tooltip("I'm a tooltip!", si::placement::centered_below());
                si::button("[centered_left]").tooltip("I'm a tooltip!", si::placement::centered_left());
                si::button("[centered_right]").tooltip("I'm a tooltip!", si::placement::centered_right());
            }
            if (auto w = si::window("popovers"))
            {
                si::text("on text").popover("I'm a text popover!");
                si::button("on button").popover("I'm a button popover!");
                si::text("placements:");
                si::button("[default]").popover("I'm a popover!", si::placement::popover_default());
                si::button("[centered_above]").popover("I'm a popover!", si::placement::centered_above());
                si::button("[centered_below]").popover("I'm a popover!", si::placement::centered_below());
                si::button("[centered_left]").popover("I'm a popover!", si::placement::centered_left());
                si::button("[centered_right]").popover("I'm a popover!", si::placement::centered_right());
                cc::unique_function<void()> rec_popover;
                rec_popover = [&] {
                    si::text("this is a recursive popover");
                    si::text("[hoverable text]").tooltip("tooltip inside popover!");
                    si::button("next popover").popover(rec_popover);
                };
                si::button("[recursive popover]").popover(rec_popover);
            }

            if (auto w = si::window("canvas"))
            {
                auto c = si::canvas({200, 200});
                c.fill(tg::aabb2({10, float(20 + frame % 10)}, {100, 40}), tg::color3::red);
                // TODO
            }

            // ui stats
            ui_merger.show_stats_ui();

            // inspector
            ui_merger.show_inspector_ui(ui.current_ui());
        });
        auto t1 = std::chrono::high_resolution_clock::now();

        // perform layouting, drawcall gen, text gen, input handling, etc.
        ui_merger.total_time = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time).count();
        ui_merger.viewport = {{0, 0}, {float(window.getWidth()), float(window.getHeight())}};
        ui_merger.mouse_pos = tg::pos2(mouse);
        ui_merger.is_lmb_down = input.get(action_mouse_left).isActive();
        ui_merger.set_record_timings(std::chrono::duration<double>(t1 - t0).count());
        ui.update(r, ui_merger);

        // upload ui data
        auto ui_proj = ctx.get_upload_buffer(sizeof(tg::mat4));
        tg::mat4 proj = tg::translation(-1.f, 1.f, 0.0f) * tg::scaling(2.f / window.getWidth(), -2.f / window.getHeight(), 1.f);
        ctx.write_to_buffer(ui_proj, proj);

        size_t vertex_byte_size = 0;
        size_t index_byte_size = 0;
        for (auto const& rl : ui_merger.get_render_data().lists)
        {
            vertex_byte_size += rl.vertices.size_bytes();
            index_byte_size += rl.indices.size_bytes();
        }
        auto ui_vertex_buffer = ctx.get_upload_buffer(vertex_byte_size);
        auto ui_index_buffer = ctx.get_upload_buffer(index_byte_size, sizeof(int));

        // upload buffer data
        {
            auto vertex_data = ctx.map_buffer(ui_vertex_buffer);
            auto index_data = ctx.map_buffer(ui_index_buffer);

            for (auto const& rl : ui_merger.get_render_data().lists)
            {
                std::memcpy(vertex_data, rl.vertices.data(), rl.vertices.size_bytes());
                std::memcpy(index_data, rl.indices.data(), rl.indices.size_bytes());

                vertex_data += rl.vertices.size_bytes();
                index_data += rl.indices.size_bytes();
            }

            ctx.unmap_buffer(ui_vertex_buffer);
            ctx.unmap_buffer(ui_index_buffer);
        }

        auto backbuffer = ctx.acquire_backbuffer(swapchain);
        auto frame = ctx.make_frame();

        {
            auto fb = frame.make_framebuffer(backbuffer);

            // clear
            {
                auto pass = fb.make_pass(pr::graphics_pass({}, vs_clear, ps_clear));
                pass.draw(3);
            }

            // ui
            {
                auto pass = fb.make_pass(pso_ui).bind(font_arg, ui_proj);
                CC_ASSERT(ui_merger.get_render_data().lists.size() == 1 && "more not supported currently");
                pass.draw(ui_vertex_buffer, ui_index_buffer);
            }

            if (show_wireframe)
            {
                auto pass = fb.make_pass(pso_ui_wf).bind(ui_proj);
                CC_ASSERT(ui_merger.get_render_data().lists.size() == 1 && "more not supported currently");
                pass.draw(ui_vertex_buffer, ui_index_buffer);
            }
        }

        frame.present_after_submit(backbuffer, swapchain);
        ctx.submit(cc::move(frame));
    }

    // make sure nothing is used anymore
    ctx.flush();
}
