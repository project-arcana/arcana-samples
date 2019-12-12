#include <nexus/fuzz_test.hh>

#include <cstdlib>

#include <clean-core/defer.hh>

#include <phantasm-renderer/backend/command_stream.hh>

FUZZ_TEST("pr backend detail - command stream")(tg::rng& rng)
{
    using namespace pr::backend;

    struct callback
    {
        int num_beg_rp = 0;
        int num_draw = 0;
        int num_transition = 0;
        int num_end_rp = 0;

        void execute(cmd::begin_render_pass const&) { ++num_beg_rp; }
        void execute(cmd::draw const&) { ++num_draw; }
        void execute(cmd::transition_resources const&) { ++num_transition; }
        void execute(cmd::end_render_pass const&) { ++num_end_rp; }

        void execute(cmd::copy_buffer const&) {}
        void execute(cmd::copy_buffer_to_texture const&) {}
        void execute(cmd::dispatch const&) {}
    };

    constexpr size_t buffer_size = 1024 * 1024;

    constexpr auto max_num_cmds = 2000u;
    static_assert(cmd::detail::max_command_size * max_num_cmds < buffer_size);

    // allocate a buffer (1kb, which is why this test costs 3ms)
    auto* const buffer = static_cast<std::byte*>(std::malloc(buffer_size));
    CC_DEFER { std::free(buffer); };

    for (auto it = 0u; it < 5u; ++it)
    {
        // create the writer (discarding any previous contents)
        command_stream_writer writer(buffer, buffer_size);

        // write random numbers of draw commands of each type
        auto const num_beg_rp = tg::uniform(rng, 0u, max_num_cmds / 4);
        auto const num_draw_cmds = tg::uniform(rng, 0u, max_num_cmds / 4);
        auto const num_transition = tg::uniform(rng, 0u, max_num_cmds / 4);
        auto const num_end_rp = tg::uniform(rng, 0u, max_num_cmds / 4);

        for (auto _ = 0u; _ < num_beg_rp; ++_)
            writer.add_command(cmd::begin_render_pass{});
        for (auto _ = 0u; _ < num_draw_cmds; ++_)
            writer.add_command(cmd::draw{});
        for (auto _ = 0u; _ < num_transition; ++_)
            writer.add_command(cmd::transition_resources{});
        for (auto _ = 0u; _ < num_end_rp; ++_)
            writer.add_command(cmd::end_render_pass{});

        // parse the buffer
        command_stream_parser parser(writer.buffer(), writer.size());

        callback callback_instance;

        // dynamically call the correct overloads of the callback
        for (cmd::detail::cmd_base const& cmd : parser)
            cmd::detail::dynamic_dispatch(cmd, callback_instance);

        // check if the amount of calls was correct
        CHECK(callback_instance.num_beg_rp == num_beg_rp);
        CHECK(callback_instance.num_draw == num_draw_cmds);
        CHECK(callback_instance.num_transition == num_transition);
        CHECK(callback_instance.num_end_rp == num_end_rp);
    }

    // zero case
    {
        command_stream_parser parser(nullptr, 0);

        callback callback_instance;
        for (cmd::detail::cmd_base const& cmd : parser)
            cmd::detail::dynamic_dispatch(cmd, callback_instance);

        // check if no calls happened
        CHECK(callback_instance.num_beg_rp == 0);
        CHECK(callback_instance.num_draw == 0);
        CHECK(callback_instance.num_transition == 0);
        CHECK(callback_instance.num_end_rp == 0);
    }
}
