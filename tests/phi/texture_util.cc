#include "texture_util.hh"

#include <cstdio>

phi::detail::unique_buffer phi_test::get_shader_binary(const char* name, const char* ending)
{
    char name_formatted[1024];
    std::snprintf(name_formatted, sizeof(name_formatted), "res/pr/liveness_sample/shader/bin/%s.%s", name, ending);
    return phi::detail::unique_buffer::create_from_binary_file(name_formatted);
}
