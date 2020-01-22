#pragma once

#include <phantasm-hardware-interface/Backend.hh>
#include <phantasm-hardware-interface/commands.hh>

namespace pr_test
{
struct temp_cmdlist
{
public:
    temp_cmdlist(phi::Backend* backend, size_t size);

    phi::command_stream_writer writer;

    void finish(bool flush_gpu = true, bool force_discard = false);

private:
    phi::Backend* _backend;
    std::byte* _buffer;
};
}
