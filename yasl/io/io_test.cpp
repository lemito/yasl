#include <yasl/io/file.h>
#include <yasl/io/input.h>
#include <yasl/io/output.h>
#include <yasl/str/builder.h>
#include <yasl/test/test.h>

#include <unistd.h>

using namespace yasl;
using namespace yasl::literals;

namespace {
    struct Point {
        int x;
        int y;
    };

    io::Output& operator<<(io::Output& out, const Point& p) {
        return out << '(' << p.x << ", " << p.y << ')';
    }

    struct Temperature {
        double celsius;
    };

    // Full control over formatting, including the spec.
    void format_value(io::Output& out, const Temperature& t, const io::FormatSpec& spec) {
        StringBuilder b;
        b.format("{:.1f}C", t.celsius);
        out.write_padded(b.view(), spec, '>');
    }

    String temp_path(StringView name) {
        return format("/tmp/yasl_io_test_{}_{}", ::getpid(), name);
    }
}

YASL_TEST(format_basic) {
    CHECK_EQ(format("{} + {} = {}", 1, 2, 3), "1 + 2 = 3");
    CHECK_EQ(format("{1}{0}{1}", 'a', 'b'), "bab");
    CHECK_EQ(format("{{literal}} {}", "x"), "{literal} x");
    CHECK_EQ(format("{:>6}|{:<6}|{:^6}", "ab", "cd", "ef"), "    ab|cd    |  ef  ");
    CHECK_EQ(format("{:*^9}", "mid"), "***mid***");
    CHECK_EQ(format("{:05}", -42), "-0042");
    CHECK_EQ(format("{:+}", 5), "+5");
    CHECK_EQ(format("{:x} {:X} {:#x} {:b} {:o}", 255, 255, 255, 5, 8), "ff FF 0xff 101 10");
    CHECK_EQ(format("{:.3f} {:.2e} {}", 3.14159, 1234.5, 0.1), "3.142 1.23e+03 0.1");
    CHECK_EQ(format("{:8.2f}|", 2.5), "    2.50|");
    CHECK_EQ(format("{:.3}", "truncate"), "tru");
    CHECK_EQ(format("{} {}", true, 'c'), "true c");
    CHECK_EQ(format("{:d}", 'A'), "65");
    CHECK_EQ(format("{}", String("owned")), "owned");
    CHECK_EQ(format("{}", "view"_sv), "view");
    CHECK_EQ(format("{}", Point{1, 2}), "(1, 2)");
    CHECK_EQ(format("[{:>10}]", Point{3, 4}), "[    (3, 4)]");
    CHECK_EQ(format("[{:>7}]", Temperature{21.55}), "[  21.6C]");
    CHECK(format("{}", static_cast<const void*>(nullptr)) == "0x0");

    CHECK_THROWS(format("{", 1), FormatError);
    CHECK_THROWS(format("}", 1), FormatError);
    CHECK_THROWS(format("{} {}", 1), FormatError);
}

YASL_TEST(output_misc) {
    io::NullOutput null;
    null << "12345" << 678;
    CHECK_EQ(null.written(), size_t(8));

    char buf[8];
    io::ArrayOutput arr(buf, sizeof(buf));
    arr << "hello, world";
    CHECK(arr.truncated());
    CHECK_EQ(arr.view(), "hello, w"_sv);

    StringBuilder b;
    b.put('-', 3);
    b << io::endl;
    CHECK_EQ(b.view(), "---\n"_sv);
}

YASL_TEST(input_parsing) {
    io::StringInput in("  42 -7 3.5 word\nsecond line\r\nthird"_sv);
    int a = 0;
    long b = 0;
    double c = 0;
    String w;
    CHECK(in >> a >> b >> c >> w);
    CHECK_EQ(a, 42);
    CHECK_EQ(b, -7L);
    CHECK_EQ(c, 3.5);
    CHECK_EQ(w, "word");

    String line;
    CHECK(in.read_line(line));
    CHECK_EQ(line, "");
    CHECK(in.read_line(line));
    CHECK_EQ(line, "second line");
    CHECK(in.read_line(line));
    CHECK_EQ(line, "third");
    CHECK(!in.read_line(line));
    CHECK(in.eof());

    io::StringInput bad("12x"_sv);
    int x = 5;
    CHECK(!(bad >> x));
    CHECK_EQ(x, 5);

    io::StringInput chars("ab"_sv);
    char ch1 = 0;
    char ch2 = 0;
    chars >> ch1 >> ch2;
    CHECK(ch1 == 'a' && ch2 == 'b');
    CHECK_EQ(chars.get(), -1);

    io::StringInput rest(String("keep me alive"));
    CHECK_EQ(rest.read_all(), "keep me alive");
}

YASL_TEST(file_roundtrip) {
    String path = temp_path("roundtrip");
    {
        io::FileOutput out(path);
        for (int i = 0; i < 20000; ++i) {
            out.format("line {} {:.2f}\n", i, i / 4.0);
        }
    }
    {
        io::FileInput in(path);
        String line;
        int n = 0;
        while (in.read_line(line)) {
            CHECK_EQ(line, format("line {} {:.2f}", n, n / 4.0));
            ++n;
        }
        CHECK_EQ(n, 20000);
    }
    {
        io::FileOutput app(path, io::OpenMode::Write | io::OpenMode::Append);
        app << "tail\n";
    }
    String all = io::read_file(path);
    CHECK(all.ends_with("line 19999 4999.75\ntail\n"));

    io::write_file(path, "replaced");
    CHECK_EQ(io::read_file(path), "replaced");
    CHECK_EQ(io::File::open(path, io::OpenMode::Read).size(), uint64(8));
    ::unlink(path.c_str());
}

YASL_TEST(file_errors) {
    try {
        io::FileInput in("/definitely/not/here");
        CHECK(false);
    } catch (const io::Error& e) {
        CHECK(e.code() != 0);
        CHECK(StringView(e.what()).starts_with("open '/definitely/not/here': "));
    }
}

YASL_TEST(file_large_unbuffered_writes) {
    String path = temp_path("large");
    String big(200000, 'z');
    {
        io::FileOutput out(io::File::open(path, io::OpenMode::Write | io::OpenMode::Create | io::OpenMode::Truncate), 1024);
        out << "head" << big << "tail";
    }
    String back = io::read_file(path);
    CHECK_EQ(back.size(), big.size() + 8);
    CHECK(back.starts_with("headzzz"));
    CHECK(back.ends_with("zzztail"));
    ::unlink(path.c_str());
}
