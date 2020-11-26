#include <nexus/test.hh>

#include <fstream>

#include <nexus/scratch_dir.hh>

TEST("scratch_dir")
{
    // obtain a folder
    auto const path = nx::open_scratch_directory();
    CHECK(!path.empty());

    // must not pre-exist (scratch dir must start out empty)
    auto const testFilePath = path + "test_file.txt";
    CHECK(!std::ifstream(testFilePath.c_str()).good());

    // must be writeable
    auto outfile = std::ofstream(testFilePath.c_str());
    CHECK(outfile.good());
    outfile.close();

    // re-open folder (deletes all contents)
    auto const path2 = nx::open_scratch_directory();
    CHECK(!path2.empty());

    // check if the file was deleted
    CHECK(!std::ifstream((path2 + "test_file.txt").c_str()).good());
}
