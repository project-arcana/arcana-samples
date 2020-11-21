#include <nexus/run.hh>

int main(int argc, char** argv)
{
    for (auto _ = 0; _ < 1000; ++_)
        nx::run(argc, argv);
}
