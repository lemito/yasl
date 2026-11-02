#include <fcntl.h>
#include <unistd.h>
#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/io/file.h>
#include <yasl/io/fs.h>
#include <yasl/io/input.h>
#include <yasl/io/output.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/builder.h>
#include <yasl/test/test.h>

#include <thread>

#include "yasl/core/exception.h"
#include "yasl/io/error.h"
#include "yasl/str/view.h"

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
void format_value(io::Output& out, const Temperature& t,
                  const io::FormatSpec& spec) {
  StringBuilder b;
  b.format("{:.1f}C", t.celsius);
  out.write_padded(b.view(), spec, '>');
}

String temp_path(StringView name) {
  return format("/tmp/yasl_io_test_{}_{}", ::getpid(), name);
}
}  // namespace

YASL_TEST(format_basic) {
  CHECK_EQ(format("{} + {} = {}", 1, 2, 3), "1 + 2 = 3");
  CHECK_EQ(format("{1}{0}{1}", 'a', 'b'), "bab");
  CHECK_EQ(format("{{literal}} {}", "x"), "{literal} x");
  CHECK_EQ(format("{:>6}|{:<6}|{:^6}", "ab", "cd", "ef"),
           "    ab|cd    |  ef  ");
  CHECK_EQ(format("{:*^9}", "mid"), "***mid***");
  CHECK_EQ(format("{:05}", -42), "-0042");
  CHECK_EQ(format("{:+}", 5), "+5");
  CHECK_EQ(format("{:x} {:X} {:#x} {:b} {:o}", 255, 255, 255, 5, 8),
           "ff FF 0xff 101 10");
  CHECK_EQ(format("{:.3f} {:.2e} {}", 3.14159, 1234.5, 0.1),
           "3.142 1.23e+03 0.1");
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

  CHECK_THROWS(format(io::runtime_format("{"), 1), FormatError);
  CHECK_THROWS(format(io::runtime_format("}"), 1), FormatError);
  CHECK_THROWS(format(io::runtime_format("{} {}"), 1), FormatError);
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

YASL_TEST(file_raw_binary_io) {
  String path = temp_path("raw_binary");
  const char bytes[] = {'\0', '\x01', '\x7f', '\x80',
                        '\xff', '\0',   '\x00', '\x42'};
  {
    io::File file = io::File::open(
        path, io::OpenMode::Read | io::OpenMode::Write | io::OpenMode::Create |
                  io::OpenMode::Truncate);
    file.write_all(bytes, sizeof(bytes));
    file.sync();
    CHECK_EQ(file.size(), uint64(sizeof(bytes)));

    file.seek(0);
    char first[3] = {};
    CHECK_EQ(file.read(first, sizeof(first)), sizeof(first));
    CHECK_EQ(StringView(first, sizeof(first)),
             StringView(bytes, sizeof(first)));

    char rest[sizeof(bytes) - sizeof(first)] = {};
    CHECK_EQ(file.read(rest, sizeof(rest)), sizeof(rest));
    CHECK_EQ(StringView(rest, sizeof(rest)),
             StringView(bytes + sizeof(first), sizeof(rest)));
    CHECK_EQ(file.read(rest, sizeof(rest)), size_t(0));
  }
  ::unlink(path.c_str());
}

YASL_TEST(file_open_modes_and_accessors) {
  String path = temp_path("open_modes");
  {
    io::FileOutput out(path, io::OpenMode::Write | io::OpenMode::Create |
                                io::OpenMode::Truncate);
    CHECK(out.file().is_open());
    out << "data";
  }
  CHECK_THROWS(
      io::File::open(path, io::OpenMode::Write | io::OpenMode::Create |
                              io::OpenMode::Exclusive),
      io::Error);
  {
    io::FileInput in(io::File::open(path, io::OpenMode::Read), 0);
    CHECK(in.file().is_open());
    CHECK_EQ(in.read_all(), "data");
  }
  ::unlink(path.c_str());
}

YASL_TEST(file_buffered_binary_roundtrip) {
  String path = temp_path("buffered_binary");
  const char bytes[] = {'h', '\0', 'e', '\0', 'l', 'l', '\0', 'o'};
  {
    io::FileOutput out(
        io::File::open(path, io::OpenMode::Write | io::OpenMode::Create |
                                 io::OpenMode::Truncate),
        2);
    out.write(StringView(bytes, sizeof(bytes)));
    out.flush();
  }
  {
    io::FileInput in(io::File::open(path, io::OpenMode::Read), 2);
    char result[sizeof(bytes)] = {};
    CHECK(in.read_exact(result, sizeof(result)));
    CHECK_EQ(StringView(result, sizeof(result)),
             StringView(bytes, sizeof(bytes)));
    CHECK_EQ(in.get(), -1);
  }
  ::unlink(path.c_str());
}

YASL_TEST(file_large_unbuffered_writes) {
  String path = temp_path("large");
  String big(200000, 'z');
  {
    io::FileOutput out(
        io::File::open(path, io::OpenMode::Write | io::OpenMode::Create |
                                 io::OpenMode::Truncate),
        1024);
    out << "head" << big << "tail";
  }
  String back = io::read_file(path);
  CHECK_EQ(back.size(), big.size() + 8);
  CHECK(back.starts_with("headzzz"));
  CHECK(back.ends_with("zzztail"));
  ::unlink(path.c_str());
}

YASL_TEST(shared_output_lines_do_not_interleave) {
  String path = temp_path("shared");
  constexpr int kThreads = 6;
  constexpr int kLines = 1500;
  {
    io::SharedOutput shared{StringView(path.view())};
    Vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&shared, t] {
        for (int k = 0; k < kLines; ++k) {
          shared.println("thread={} line={} {:->40}", t, k, "end");
        }
      });
    }
    for (auto& th : threads) {
      th.join();
    }
    shared.flush();
  }
  String all = io::read_file(path.view());
  Vector<int> seen(kThreads, 0);
  size_t lines = 0;
  for (StringView rest = all.view(); !rest.empty();) {
    size_t nl = rest.find('\n');
    CHECK(nl != StringView::npos);
    StringView line = rest.first(nl);
    rest = rest.drop_front(nl + 1);
    // "thread=T line=K -----...-----end"
    auto first = line.split_once(' ');
    CHECK(first.has_value());
    auto second = first->second.split_once(' ');
    CHECK(second.has_value());
    auto t = first->first.strip_prefix("thread=")->parse<int>();
    auto k = second->first.strip_prefix("line=")->parse<int>();
    CHECK(t.has_value() && k.has_value());
    CHECK(*t >= 0 && *t < kThreads);
    CHECK_EQ(seen[size_t(*t)], *k);
    CHECK_EQ(second->second, String(37, '-') + "end");
    ++seen[size_t(*t)];
    ++lines;
  }
  CHECK_EQ(lines, size_t(kThreads * kLines));
  ::unlink(path.c_str());
}

YASL_TEST(locked_keeps_pieces_together) {
  String path = temp_path("locked");
  {
    io::SharedOutput shared{StringView(path.view())};
    Vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
      threads.emplace_back([&shared, t] {
        for (int k = 0; k < 500; ++k) {
          io::Locked out = shared.lock();
          out << "[" << t << ":";
          out << k << "]\n";
        }
      });
    }
    for (auto& th : threads) {
      th.join();
    }
    shared.flush();
  }
  String all = io::read_file(path.view());
  size_t lines = 0;
  for (StringView rest = all.view(); !rest.empty(); ++lines) {
    size_t nl = rest.find('\n');
    StringView line = rest.first(nl);
    CHECK(line.starts_with('[') && line.ends_with(']'));
    CHECK(line.find(':') != StringView::npos);
    rest = rest.drop_front(nl + 1);
  }
  CHECK_EQ(lines, size_t(2000));
  ::unlink(path.c_str());
}

YASL_TEST(read_line_from_stdin) {
  String path = temp_path("stdin");
  io::write_file(path.view(), "first\nsecond\n");
  int saved = ::dup(0);
  int fd = ::open(path.c_str(), O_RDONLY);
  ::dup2(fd, 0);
  ::close(fd);
  Optional<String> a = io::read_line();
  Optional<String> b = io::read_line();
  Optional<String> c = io::read_line();
  ::dup2(saved, 0);
  ::close(saved);
  ::unlink(path.c_str());
  CHECK_EQ(*a, "first");
  CHECK_EQ(*b, "second");
  CHECK(!c.has_value());

  StringBuilder sb;

  sb << "six-seven";
  io::out() << sb.take();
}

YASL_TEST(input_read_exact) {
  io::StringInput in(StringView("abcdef"));
  char buf[8];
  CHECK(in.read_exact(buf, 4));
  CHECK_EQ(StringView(buf, 4), "abcd");
  CHECK(in.ok());
  CHECK(!in.read_exact(buf, 4));
  CHECK_EQ(StringView(buf, 2), "ef");
  CHECK(!in.ok());
}

YASL_TEST(input_rejects_oversized_token) {
  String token(513, '1');
  io::StringInput in(token);
  int value = 0;
  CHECK(!(in >> value));
  CHECK_EQ(value, 0);
  CHECK(!in.ok());
}

YASL_TEST(input_small_buffer_boundaries) {
  String path = temp_path("small_buffer");
  io::write_file(path, " one\t two\r\nthree");

  io::FileInput in(io::File::open(path.view(), io::OpenMode::Read), 3);
  String word;
  CHECK(in.read_word(word));
  CHECK_EQ(word, "one");
  CHECK(in.read_word(word));
  CHECK_EQ(word, "two");

  String line;
  CHECK(in.read_line(line));
  CHECK_EQ(line, "");
  CHECK(in.read_line(line));
  CHECK_EQ(line, "three");
  CHECK(!in.read_line(line));
  CHECK(in.eof());
  ::unlink(path.c_str());
}

YASL_TEST(runtime_format_errors) {
  StringBuilder out;
  CHECK_THROWS(out.format(io::runtime_format("{"), 1), FormatError);
  CHECK_THROWS(out.format(io::runtime_format("{:.}"), 1.0), FormatError);
  CHECK_THROWS(out.format(io::runtime_format("{} {}"), 1), FormatError);
  CHECK_THROWS(out.format(io::runtime_format("{:1"), 1), FormatError);
}

YASL_TEST(io_copy_between_streams) {
  mem::Tracking t;
  String text = repeat("0123456789", 5000, &t);
  io::StringInput in(text);
  StringBuilder sb(&t);
  CHECK_EQ(io::copy(in, sb), text.size());
  CHECK(in.eof());
  CHECK_EQ(sb.take(), text);

  io::StringInput empty(StringView(""));
  io::NullOutput sink;
  CHECK_EQ(io::copy(empty, sink), 0u);
}

YASL_TEST(io_copy_through_file_buffer) {
  String text = repeat("line\n", 40000);
  String path = String("/tmp/yasl_copy_test_") + to_string(this_thread::id());
  io::write_file(path, text);
  io::FileInput in(io::File::open(path.view(), io::OpenMode::Read), 4096);
  StringBuilder sb;
  CHECK_EQ(io::copy(in, sb), text.size());
  CHECK_EQ(sb.take(), text);
  io::fs::remove(io::Path(path.view()));
}

YASL_TEST(io_limited_input) {
  io::StringInput base(StringView("0123456789"));
  {
    io::LimitedInput lim(base, 4);
    CHECK_EQ(lim.remaining(), 4u);
    CHECK_EQ(lim.get(), '0');
    CHECK_EQ(lim.remaining(), 3u);
    CHECK_EQ(lim.read_all(), "123");
    CHECK(lim.eof());
    CHECK_EQ(lim.remaining(), 0u);
  }
  CHECK_EQ(base.read_all(), "456789");

  // Destroying it early gives back the unread part.
  io::StringInput base2(StringView("abcdef"));
  {
    io::LimitedInput lim(base2, 5);
    CHECK_EQ(lim.get(), 'a');
    CHECK_EQ(lim.get(), 'b');
  }
  CHECK_EQ(base2.read_all(), "cdef");

  // A limit longer than the data stops at the end of the data.
  io::StringInput base3(StringView("xy"));
  io::LimitedInput lim3(base3, 100);
  CHECK_EQ(lim3.read_all(), "xy");
}

YASL_TEST(io_tee_output) {
  StringBuilder a, b;
  io::TeeOutput tee(a, b);
  tee << "x=" << 42 << '\n';
  tee.format("{:>4}", 7);
  tee.flush();
  CHECK_EQ(a.take(), "x=42\n   7");
  CHECK_EQ(b.take(), "x=42\n   7");
}

YASL_TEST(io_file_test) {
  try {
    io::FileInput in("no_this.file");
  } catch (yasl::io::Error& e) {
    io::err() << StringView(e.what());
    return;
  }
  io::FileOutput out("out.txt");
  out << "A" << (10 + 2) << '\n';

  io::FileInput in("out.txt");
  String line;
  if (!in.read_line(line)) {
    io::err() << StringView("Empty file");
    return;
  }
  CHECK_EQ(line[0], 'A');
  CHECK_EQ(line[1], '1');
  CHECK_EQ(line[2], '2');
}