#include <nexus/test.hh>

#include <babel-serializer/file.hh>

TEST("file")
{
    auto tmp_file = "_tmp_babel_file";
    babel::file::write(tmp_file, "hello world!");
    CHECK(babel::file::read_all_text(tmp_file) == "hello world!");
}
