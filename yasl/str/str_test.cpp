#include <yasl/mem/arena.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/builder.h>
#include <yasl/str/str.h>
#include <yasl/str/view.h>
#include <yasl/test/test.h>

#include <string>
#include <thread>
#include <vector>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(view_basics) {
  constexpr StringView v = "hello, world"_sv;
  static_assert(v.size() == 12);
  static_assert(v.starts_with("hello"));
  static_assert(v.find(',') == 5);
  static_assert(v.find("world") == 7);
  CHECK_EQ(v.substr(7), "world"_sv);
  CHECK_EQ(v.substr(100), ""_sv);
  CHECK_EQ(v.substr(7, 100), "world"_sv);
  CHECK_EQ(v.first(5), "hello"_sv);
  CHECK_EQ(v.last(5), "world"_sv);
  CHECK_EQ(v.drop_front(7).drop_back(2), "wor"_sv);
  CHECK(v.contains("o, w"));
  CHECK(!v.contains('z'));
  CHECK_EQ(v.rfind('o'), size_t(8));
  CHECK_EQ(v.rfind("o"), size_t(8));
  CHECK_EQ(v.rfind("hello"), size_t(0));
  CHECK_EQ(v.rfind('o', 5), size_t(4));
  CHECK_EQ(v.find("xyz"), StringView::npos);
  CHECK(v.ends_with('d'));
  CHECK(StringView() == ""_sv);
  CHECK(StringView(nullptr).empty());
  CHECK("abc"_sv < "abd"_sv);
  CHECK("ab"_sv < "abc"_sv);
  std::string owner = "xyz";
  CHECK_EQ(StringView(owner), "xyz"_sv);
  CHECK_EQ(hash_value("abc"_sv), hash_value(String("abc")));
}

YASL_TEST(view_trim_split) {
  CHECK_EQ("  \t padded \n"_sv.trim(), "padded"_sv);
  CHECK_EQ("   "_sv.trim(), ""_sv);
  CHECK_EQ("xxabcxx"_sv.trim("x"), "abc"_sv);
  CHECK_EQ("  a "_sv.trim_left(), "a "_sv);
  CHECK_EQ("  a "_sv.trim_right(), "  a"_sv);

  auto kv = "key = value"_sv.split_once('=');
  CHECK(kv.has_value());
  CHECK_EQ(kv->first.trim(), "key"_sv);
  CHECK_EQ(kv->second.trim(), "value"_sv);
  CHECK(!"novalue"_sv.split_once('=').has_value());
  CHECK_EQ("a.b.c"_sv.rsplit_once('.')->second, "c"_sv);
  CHECK_EQ("a::b::c"_sv.split_once("::")->second, "b::c"_sv);

  std::vector<std::string> parts;
  for (StringView p : "a,,b,c"_sv.split(',')) {
    parts.emplace_back(p.data(), p.size());
  }
  CHECK((parts == std::vector<std::string>{"a", "", "b", "c"}));

  parts.clear();
  for (StringView p : ""_sv.split(',')) {
    parts.emplace_back(p.data(), p.size());
  }
  CHECK((parts == std::vector<std::string>{""}));

  parts.clear();
  for (StringView p : "1<>2<>"_sv.split("<>")) {
    parts.emplace_back(p.data(), p.size());
  }
  CHECK((parts == std::vector<std::string>{"1", "2", ""}));
}

YASL_TEST(view_parse) {
  CHECK("42"_sv.parse<int>() == 42);
  CHECK("-7"_sv.parse<long>() == -7L);
  CHECK("+7"_sv.parse<unsigned>() == 7u);
  CHECK(!"42x"_sv.parse<int>().has_value());
  CHECK(!""_sv.parse<int>().has_value());
  CHECK(!"-"_sv.parse<int>().has_value());
  CHECK(!"300"_sv.parse<uint8>().has_value());
  CHECK(!"-1"_sv.parse<uint32>().has_value());
  CHECK("-128"_sv.parse<int8>() == int8(-128));
  CHECK(!"-129"_sv.parse<int8>().has_value());
  CHECK("18446744073709551615"_sv.parse<uint64>() == 18446744073709551615ull);
  CHECK(!"18446744073709551616"_sv.parse<uint64>().has_value());
  CHECK("-9223372036854775808"_sv.parse<int64>() == min_value<int64>());
  CHECK("2.5"_sv.parse<double>() == 2.5);
  CHECK("-1e3"_sv.parse<double>() == -1000.0);
  CHECK(".5"_sv.parse<double>() == 0.5);
  CHECK("5."_sv.parse<double>() == 5.0);
  CHECK(!"."_sv.parse<double>().has_value());
  CHECK(!"1e"_sv.parse<double>().has_value());
  CHECK(!"1.2.3"_sv.parse<double>().has_value());
}

YASL_TEST(string_basic) {
  String s = "hello";
  CHECK_EQ(s.size(), size_t(5));
  CHECK_EQ(s, "hello");
  CHECK("hello" == s);
  CHECK(s == String("hello"));
  CHECK(s < "help");
  CHECK(String("a") < String("b"));
  s += ", world";
  s += '!';
  CHECK_EQ(s, "hello, world!");
  CHECK_EQ(s.find("world"), size_t(7));
  CHECK_EQ(s.substr(7, 5), "world");
  CHECK_EQ(s + "?", "hello, world!?");
  CHECK_EQ("<" + s, "<hello, world!");
  CHECK_EQ(s + s, "hello, world!hello, world!");
  CHECK_THROWS(s.at(100), OutOfRange);

  String e;
  CHECK(e.empty());
  CHECK_EQ(StringView(e.c_str()), ""_sv);
  e.clear();
  CHECK_EQ(e.capacity(), String::inline_capacity);
}

YASL_TEST(string_sso) {
  static_assert(sizeof(String) == 24);
  mem::Tracking t;
  {
    String a("0123456789abcde", &t);
    CHECK_EQ(a.size(), size_t(15));
    CHECK_EQ(a.capacity(), String::inline_capacity);
    CHECK_EQ(t.stats().live(), size_t(0));
    CHECK_EQ(StringView(a.c_str()), a.view());
    CHECK(!a.is_shared());

    String b = a;
    CHECK(a.data() != b.data());
    b.set(0, 'X');
    CHECK_EQ(a, "0123456789abcde");
    CHECK_EQ(b, "X123456789abcde");

    String c = a;
    c.push_back('f');
    CHECK_EQ(c, "0123456789abcdef");
    CHECK_EQ(t.stats().live(), size_t(1));
    CHECK(c.capacity() > String::inline_capacity);

    String d = c;
    CHECK(d.is_shared());
    d.erase(3);
    CHECK_EQ(d, "012");
    CHECK(!d.is_shared());
    CHECK(!c.is_shared());
    CHECK_EQ(c, "0123456789abcdef");
    CHECK_EQ(t.stats().live(), size_t(1));

    String m = yasl::move(c);
    CHECK(c.empty());
    CHECK_EQ(m, "0123456789abcdef");
    String s = "tiny";
    s.swap(m);
    CHECK_EQ(s, "0123456789abcdef");
    CHECK_EQ(m, "tiny");
    s = m;
    CHECK_EQ(s, "tiny");
    String& same = s;
    s = same;
    CHECK_EQ(s, "tiny");
    CHECK_EQ(t.stats().live(), size_t(0));

    String e("", &t);
    CHECK(e.empty());
    CHECK_EQ(StringView(e.c_str()), ""_sv);
    e.append(String(40, 'q', &t));
    e.clear();
    e += 'z';
    CHECK_EQ(e, "z");
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(string_cow) {
  mem::Tracking t;
  {
    String a("a shared buffer beyond sso", &t);
    String b = a;
    CHECK_EQ(a.data(), b.data());
    CHECK(a.is_shared());
    CHECK_EQ(a.use_count(), size_t(2));
    CHECK_EQ(t.stats().live(), size_t(1));

    b.set(0, 'A');
    CHECK(a.data() != b.data());
    CHECK_EQ(a, "a shared buffer beyond sso");
    CHECK_EQ(b, "A shared buffer beyond sso");
    CHECK(!a.is_shared());
    CHECK_EQ(t.stats().live(), size_t(2));

    String c = a;
    c += "!";
    CHECK_EQ(a, "a shared buffer beyond sso");
    CHECK_EQ(c, "a shared buffer beyond sso!");

    String d = a;
    d.clear();
    CHECK_EQ(a, "a shared buffer beyond sso");

    String e = a;
    char* p = e.mut_data();
    p[3] = 'H';
    CHECK_EQ(a, "a shared buffer beyond sso");
    CHECK_EQ(e, "a sHared buffer beyond sso");

    String f(a, mem::system_resource());
    CHECK(f.data() != a.data());
    CHECK_EQ(f, a);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(string_self_aliasing) {
  String s = "abc";
  s.append(s);
  CHECK_EQ(s, "abcabc");
  s.append(s.view().substr(1, 2));
  CHECK_EQ(s, "abcabcbc");
  s.insert(0, s.view().last(2));
  CHECK_EQ(s, "bcabcabcbc");
  s.replace(2, 3, s.view().first(4));
  CHECK_EQ(s, "bcbcababcbc");
  s = s.view().substr(2, 3);
  CHECK_EQ(s, "bca");
}

YASL_TEST(string_edit) {
  String s = "0123456789";
  s.erase(2, 3);
  CHECK_EQ(s, "0156789");
  s.insert(2, "234");
  CHECK_EQ(s, "0123456789");
  s.replace(0, 5, "ab");
  CHECK_EQ(s, "ab56789");
  s.resize(3);
  CHECK_EQ(s, "ab5");
  s.resize(5, '.');
  CHECK_EQ(s, "ab5..");
  s.pop_back();
  CHECK_EQ(s, "ab5.");
  s.erase(1);
  CHECK_EQ(s, "a");
  s.append(3, 'z');
  CHECK_EQ(s, "azzz");
  s = "";
  CHECK(s.empty());
}

YASL_TEST(string_threads_share) {
  String base(1000, 'x');
  std::vector<std::thread> threads;
  for (int k = 0; k < 4; ++k) {
    threads.emplace_back([base] {
      for (int i = 0; i < 1000; ++i) {
        String copy = base;
        if (i % 10 == 0) {
          copy.set(0, 'y');
        }
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  CHECK_EQ(base.use_count(), size_t(1));
  CHECK_EQ(base[0], 'x');
}

YASL_TEST(builder_basic) {
  StringBuilder b;
  b << "int=" << 42 << " neg=" << -7 << " u=" << 18446744073709551615ull;
  b << " c=" << 'x' << " b=" << true << " d=" << 0.25;
  CHECK_EQ(b.view(),
           "int=42 neg=-7 u=18446744073709551615 c=x b=true d=0.25"_sv);

  b.clear();
  b.format("{}-{:03}", "id", 7).append(2, '!');
  CHECK_EQ(b.view(), "id-007!!"_sv);

  String snapshot = b.str();
  CHECK_EQ(snapshot.data(), b.c_str());
  b << "more";
  CHECK_EQ(snapshot, "id-007!!");
  CHECK_EQ(b.view(), "id-007!!more"_sv);

  String taken = b.take();
  CHECK(b.empty());
  CHECK_EQ(taken, "id-007!!more");

  StringBuilder cont(String("start:"));
  cont << 1;
  CHECK_EQ(cont.view(), "start:1"_sv);
}

YASL_TEST(builder_large_and_arena) {
  mem::Arena arena;
  StringBuilder b(&arena);
  for (int i = 0; i < 10000; ++i) {
    b << i << ',';
  }
  String s = b.take();
  CHECK_EQ(s.resource(), static_cast<mem::Resource*>(&arena));
  CHECK(s.starts_with("0,1,2,"));
  CHECK(s.ends_with("9998,9999,"));

  StringBuilder big;
  String chunk(5000, 'q');
  big << "<" << chunk << ">";
  CHECK_EQ(big.size(), size_t(5002));
  CHECK_EQ(to_string(12.5), "12.5");
}

YASL_TEST(view_lines_and_words) {
  auto collect = [](auto range) {
    std::vector<std::string> out;
    for (StringView part : range) {
      out.emplace_back(part.data(), part.size());
    }
    return out;
  };
  using V = std::vector<std::string>;
  CHECK((collect(StringView("a\nb\r\nc").lines()) == V{"a", "b", "c"}));
  CHECK((collect(StringView("a\nb\n").lines()) == V{"a", "b"}));
  CHECK((collect(StringView("\n\nx").lines()) == V{"", "", "x"}));
  CHECK((collect(StringView("").lines()) == V{}));
  CHECK((collect(StringView("  one  two\tthree\n four ").split_whitespace()) ==
         V{"one", "two", "three", "four"}));
  CHECK((collect(StringView("   ").split_whitespace()) == V{}));
  CHECK((collect(StringView("solo").split_whitespace()) == V{"solo"}));
}

YASL_TEST(view_strip_count) {
  StringView v = "prefix-body-suffix";
  CHECK_EQ(*v.strip_prefix("prefix-"), "body-suffix");
  CHECK(!v.strip_prefix("nope").has_value());
  CHECK_EQ(*v.strip_suffix("-suffix"), "prefix-body");
  CHECK_EQ(*v.strip_suffix('x'), "prefix-body-suffi");
  CHECK_EQ(*v.strip_prefix('p'), "refix-body-suffix");
  CHECK_EQ(StringView("abababa").count("aba"), size_t(2));
  CHECK_EQ(StringView("a,b,,c").count(','), size_t(3));
  CHECK_EQ(StringView("abc").count(""), size_t(0));
}

YASL_TEST(string_replace_repeat_case_join) {
  CHECK_EQ(replace_all("a-b-c", "-", "+"), "a+b+c");
  CHECK_EQ(replace_all("aaaa", "aa", "b"), "bb");
  CHECK_EQ(replace_all("abc", "", "x"), "abc");
  CHECK_EQ(replace_all("abc", "abc", ""), "");
  CHECK_EQ(repeat("ab", 3), "ababab");
  CHECK_EQ(repeat("ab", 0), "");
  CHECK_EQ(to_upper("Hello, World 1"), "HELLO, WORLD 1");
  CHECK_EQ(to_lower("Hello, World 1"), "hello, world 1");
  String s = "MiXed";
  CHECK_EQ(s.to_lower(), "mixed");
  CHECK_EQ(s.to_upper(), "MIXED");
  CHECK_EQ(s.repeat(2), "MiXedMiXed");
  CHECK_EQ(s.replace_all("X", "x"), "Mixed");
  std::vector<std::string> names = {"ann", "bob", "cy"};
  CHECK_EQ(join(", ", names), "ann, bob, cy");
  std::vector<std::string> none;
  CHECK_EQ(join(",", none), "");
  CHECK_EQ(join("-", std::vector<std::string>{"solo"}), "solo");
}

YASL_TEST(view_try_parse) {
  CHECK_EQ(*StringView("42").try_parse<int>(), 42);
  CHECK_EQ(*StringView("-7").try_parse<int64>(), int64(-7));
  CHECK_EQ(*StringView("2.5").try_parse<double>(), 2.5);
  CHECK(StringView("").try_parse<int>().error().kind() ==
        ParseErrorKind::Empty);
  CHECK(StringView("12x").try_parse<int>().error().kind() ==
        ParseErrorKind::InvalidDigit);
  CHECK(StringView("-").try_parse<int>().error().kind() ==
        ParseErrorKind::InvalidDigit);
  CHECK(StringView("-5").try_parse<unsigned>().error().kind() ==
        ParseErrorKind::InvalidDigit);
  CHECK(StringView("300").try_parse<uint8>().error().kind() ==
        ParseErrorKind::Overflow);
  CHECK(StringView("99999999999999999999").try_parse<int64>().error().kind() ==
        ParseErrorKind::Overflow);
  CHECK(StringView("1.5.2").try_parse<double>().error().kind() ==
        ParseErrorKind::InvalidDigit);
  auto r = StringView("abc").try_parse<int>();
  CHECK(r.is_err());
  CHECK(StringView(r.error().what()).contains("invalid"));
  CHECK_EQ(StringView("5").parse<int>().value_or(0), 5);
}

YASL_TEST(utf8_chars_count_validate) {
  StringView s = "h\xC3\xA9llo \xE2\x82\xAC \xF0\x9F\x98\x80";
  std::vector<char32_t> cps;
  for (char32_t c : s.chars()) {
    cps.push_back(c);
  }
  CHECK((cps == std::vector<char32_t>{'h', 0xE9, 'l', 'l', 'o', ' ', 0x20AC,
                                      ' ', 0x1F600}));
  CHECK_EQ(s.char_count(), size_t(9));
  CHECK(s.is_valid_utf8());
  CHECK(StringView("").is_valid_utf8());
  CHECK_EQ(StringView("").char_count(), size_t(0));

  // overlong, lone continuation, truncated, surrogate, too large
  for (const char* bad : {"\xC0\x80", "\x80", "\xE2\x82", "\xED\xA0\x80",
                          "\xF4\x90\x80\x80", "\xFF", "a\xC3"}) {
    CHECK(!StringView(bad).is_valid_utf8());
  }
  std::vector<char32_t> broken;
  for (char32_t c : StringView("a\xFF"
                               "b")
                        .chars()) {
    broken.push_back(c);
  }
  CHECK((broken == std::vector<char32_t>{'a', 0xFFFD, 'b'}));
}

YASL_TEST(utf8_encode_roundtrip) {
  StringBuilder b;
  for (char32_t cp :
       {char32_t('A'), char32_t(0xE9), char32_t(0x20AC), char32_t(0x1F600),
        char32_t(0x10FFFF), char32_t(0x7FF), char32_t(0x800)}) {
    b.push_char(cp);
  }
  b.push_char(0xD800);    // surrogate -> U+FFFD
  b.push_char(0x110000);  // out of range -> U+FFFD
  String out = b.take();
  CHECK(out.view().is_valid_utf8());
  std::vector<char32_t> back;
  for (char32_t c : out.view().chars()) {
    back.push_back(c);
  }
  CHECK((back == std::vector<char32_t>{'A', 0xE9, 0x20AC, 0x1F600, 0x10FFFF,
                                       0x7FF, 0x800, 0xFFFD, 0xFFFD}));
}
