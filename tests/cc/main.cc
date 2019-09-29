#include <doctest.hh>

int main(int argc, char **argv)
{
    doctest::Context context;
    context.applyCommandLine(argc, argv);

    auto res = context.run();

    return res;
}
