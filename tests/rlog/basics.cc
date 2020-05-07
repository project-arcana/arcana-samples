#include <nexus/test.hh>

#include <rich-log/log.hh>
#include <rich-log/logger.hh>

TEST("basic logging")
{
    rlog::enable_win32_colors();
    rlog::set_current_thread_name("td#0");
    {
        int x = 3;
        LOG() << 1 << "hello" << true;

        LOG(severity::trace()) << 1.f;
        LOG(severity::debug()) << 2u;
        LOG(severity::info()) << 3.0;
        LOG(severity::warning()) << 4ull;
        LOG(severity::error()) << '5';

        LOG(info) << 2;
        LOG(error) << x;
        LOG(debug) << 2;
        LOG(warning) << 2;
        LOG(info, severity("FATAL")) << 2;
        LOG(info)("bla: {} {}", 7, true);
        LOG(info)("test: {}", 7) << 7 + 2;
        LOG(no_sep) << 1 << 2 << 3;
        LOG(sep("::")) << 1 << 2 << 3;
        //                LOG_EXPR(1 + x);
        //                LOG_EXPR(2 + x, error);
    }

    {
        constexpr auto const custom_domain = rlog::domain("MYDOMAIN");

        auto const custom_log = [&](auto... additional) { return rlog::MessageBuilder(custom_domain, additional...); };

        custom_log()("test");
        custom_log(rlog::err_out)("test with error");
    }
}
