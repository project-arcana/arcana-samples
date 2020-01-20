#pragma once

#include <phantasm-renderer/backend/Backend.hh>
#include <phantasm-renderer/backend/commands.hh>

namespace pr_test
{
struct temp_cmdlist
{
public:
    temp_cmdlist(pr::backend::Backend* backend, size_t size);

    pr::backend::command_stream_writer writer;

    void finish(bool flush_gpu = true, bool force_discard = false);

private:
    pr::backend::Backend* _backend;
    std::byte* _buffer;
};
}
