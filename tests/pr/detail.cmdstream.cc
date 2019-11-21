#include <nexus/test.hh>

#include <cstdlib>
#include <iostream>

#include <clean-core/defer.hh>

#include <phantasm-renderer/backend/command_stream.hh>


TEST("pr backend detail - command stream")
{
    using namespace pr::backend;

    constexpr size_t buffer_size = 1024 * 1024;

    // allocate a buffer
    char* const buffer = static_cast<char*>(std::malloc(buffer_size));
    CC_DEFER { std::free(buffer); };

    // create the writer
    command_stream_writer writer(buffer, buffer_size);

    // write 10 draw commands
    constexpr auto num_draw_cmds = 10;
    static_assert(sizeof(cmd::draw) * num_draw_cmds + sizeof(cmd::final_command) < buffer_size);

    for (auto _ = 0; _ < num_draw_cmds; ++_)
        writer.add_command(cmd::draw{});
    writer.finalize();

    // parse the buffer
    command_stream_parser parser(buffer);

    // check that all parsed commands are draws, and that the number is right
    auto num_read_draws = 0;

    struct callback
    {
        int num_drawcalls = 0;

        void execute(cmd::begin_render_pass const&) {}
        void execute(cmd::draw const&) { ++num_drawcalls; }
        void execute(cmd::transition_resources const&) {}
        void execute(cmd::end_render_pass const&) {}
        void execute(cmd::final_command const&) {}
    };

    callback callback_instance;
    REQUIRE(callback_instance.num_drawcalls == 0);

    for (cmd::detail::cmd_base const& x : parser)
    {
        CHECK(x.type == cmd::detail::cmd_type::draw);

        ++num_read_draws;
        cmd::detail::dynamic_dispatch(x, callback_instance);
    }

    CHECK(num_read_draws == num_draw_cmds);
    CHECK(num_read_draws == callback_instance.num_drawcalls);
}
