#include <nexus/test.hh>

#include <rich-log/log.hh>
#include <rich-log/logger.hh>

TEST("basic logging")
{
    rlog::enable_win32_colors();
    rlog::set_current_thread_name("td#0");


    {
        LOG("format {}", 5);

        int x = 3;
        LOG << 1 << "hello" << true;
        LOG() << x << "goodbye" << false;

        LOG_INFO() << 1.f;
        LOG_INFO("test {}", "test");


        LOG_DEBUG << 2u;
        LOG_WARN << 3.0;
        LOG_ERROR << '5';

        LOG("bla: {} {}", 7, true);
        LOG("test: {}", 7) << 7 + 2;
        //                LOG_EXPR(1 + x);
        //                LOG_EXPR(2 + x, error);
    }

    //    {
    //        constexpr auto const custom_domain = rlog::domain("MYDOMAIN");

    //        auto const custom_log = [&](auto... additional) { return rlog::MessageBuilder(custom_domain, additional...); };

    //        custom_log()("test");
    //        custom_log(rlog::err_out)("test with error");
    //    }
}
