#include "sample_util.hh"

pr_test::temp_cmdlist::temp_cmdlist(pr::backend::Backend* backend, size_t size)
  : _backend(backend), _buffer(static_cast<std::byte*>(std::malloc(size)))
{
    writer.initialize(_buffer, size);
}

void pr_test::temp_cmdlist::finish(bool flush_gpu)
{
    auto const cmdlist = _backend->recordCommandList(writer.buffer(), writer.size());
    std::free(_buffer);

    _backend->submit(cc::span{cmdlist});

    if (flush_gpu)
        _backend->flushGPU();
}
