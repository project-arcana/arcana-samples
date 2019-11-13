#include <nexus/test.hh>

#include <rich-log/log.hh>
#include <rich-log/logger.hh>

TEST("basic logging")
{
    {
        int x = 3;
        LOG() << 1 << "hello" << true;
        LOG(prefix("[bla] ")) << 1.f;
        LOG(info) << 2;
        LOG(error) << x;
        LOG(debug) << 2;
        LOG(warning) << 2;
        LOG(info, prefix("---")) << 2;
        LOG(info)("bla: {} {}", 7, true);
        LOG(info)("test: {}", 7) << 7 + 2;
        LOG(no_sep) << 1 << 2 << 3;
        LOG(sep("::")) << 1 << 2 << 3;
        LOG_EXPR(1 + x);
        LOG_EXPR(2 + x, error);
    }
}