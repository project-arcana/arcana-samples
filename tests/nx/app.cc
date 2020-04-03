#include <nexus/app.hh>

#include <iostream>

APP("myapp")
{
    // TODO: support for optionals

    int nr = -1;
    bool b = false;
    cc::string name;

    nx::args args("My App", "just some sample app.");
    args.version("0.0.1-alpha");
    args.add({"f", "flag"}, "some flag") //
        .add(nr, {"i", "nr"}, "number")
        .group("other")
        .add(b, "b", "some bool")
        .add(name, {"n", "name"}, "some name");

    if (!args.parse())
        return;

    //
    std::cout << "executing Nexus app 'myapp'" << std::endl;
    std::cout << "  args has 'f': " << args.has("f") << std::endl;
    std::cout << "  args has 'i': " << args.has("i") << std::endl;
    std::cout << "  args has 'b': " << args.has("b") << std::endl;
    std::cout << "  args has 'n': " << args.has("n") << std::endl;
}