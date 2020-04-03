#include <nexus/test.hh>

#include <nexus/args.hh>

#include <clean-core/set.hh>
#include <clean-core/vector.hh>

TEST("nx::args")
{
    // api
    {
        bool b = false;
        int i = 0;
        double d = 0;
        cc::string s;
        float f = 0;
        char c = 0;
        cc::vector<bool> bs;
        cc::set<int> is;

        auto args = nx::args("test") //
                        .version("1.2.3")
                        .disable_help()
                        .group("optionals")
                        .add("x", "desc")
                        .add({"c", "cc"}, "count")
                        .group("typed optionals")
                        .add<int>("g", "desc g")
                        .add<float>({"l", "ofloat"}, "with desc")
                        .group("vars")
                        .add(b, "b", "bool")
                        .add(c, {"u", "char"}, "desc")
                        .add(d, "h", "double")
                        .add(i, "int", "int")
                        .add(s, "string", "some string")
                        .add(f, {"f", "float"}, "some float")
            //                .add(bs, "bs", "bool list")
            //                .add(is, "is", "int set")
            ;

        char const* argv[] = {"-b"};
        auto ok = args.parse(1, argv);
        CHECK(ok);
    }

    {
        auto args = nx::args("test") //
                        .add({"c", "count"}, "")
                        .add({"t", "test"}, "");

        char const* argv[] = {"-c", "--test"};
        auto ok = args.parse(sizeof(argv) / sizeof(argv[0]), argv);
        CHECK(ok);
    }

    {
        bool not_set = false;
        bool flag = false;
        char k = 0;
        int i = 0;
        float f = 0;
        short s = 0;
        cc::string txt;
        cc::string txt2;
        cc::string txt3;
        cc::string txt4;
        bool a = false;
        int n = 0;

        auto args = nx::args() //
                        .add(not_set, "not-set", "")
                        .add(flag, "b", "")
                        .add({"c", "count"}, "")
                        .add(k, "k", "")
                        .add(i, "i", "")
                        .add(f, "f", "")
                        .add(s, "s", "")
                        .add(txt, "text", "")
                        .add(txt2, "t", "")
                        .add(txt3, "text3", "")
                        .add(txt4, "text4", "")
                        .add(a, "a", "")
                        .add(n, "n", "");

        char const* argv[] = {
            "-b",                          //
            "-c",                          //
            "-ccc",                        //
            "--count",                     //
            "-ku",                         //
            "abc",                         //
            "-i=8",                        //
            "-f1.5",                       //
            "-s",           "17",          //
            "--text=hello",                //
            "-tblub",                      //
            "def",                         //
            "--text3",      "hello world", //
            "--text4",      "--",          //
            "-an100",                      //
            "--",                          //
            "-a",                          //
            "--x",                         //
            "end",                         //
        };
        auto ok = args.parse(sizeof(argv) / sizeof(argv[0]), argv);
        CHECK(ok);

        CHECK(!args.has("not-set"));
        CHECK(args.has("c"));
        CHECK(args.idx_of("k") < args.idx_of("t"));
        CHECK(!not_set);
        CHECK(flag);
        CHECK(args.count_of("c") == 5);
        CHECK(k == 'u');
        CHECK(i == 8);
        CHECK(f == 1.5f);
        CHECK(s == 17);
        CHECK(txt == "hello");
        CHECK(txt2 == "blub");
        CHECK(txt3 == "hello world");
        CHECK(txt4 == "--");
        CHECK(a);
        CHECK(n == 100);
        CHECK(args.get_or("s", 9) == 17);
        CHECK(args.get_or("undef", 9) == 9);

        CHECK(args.positional_args() == cc::vector<cc::string>{"abc", "def", "-a", "--x", "end"});
    }
}
