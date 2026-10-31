#include <yasl/mem/tracking.h>
#include <yasl/str/builder.h>
#include <yasl/str/str.h>
#include <yasl/str/view.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(string_edge_empty) {
  String e;
  CHECK(e.empty());
  CHECK_EQ(e.size(), size_t(0));
  CHECK_EQ(e.capacity(), String::inline_capacity);
  CHECK(e == ""_sv);
  CHECK_EQ(StringView(e.c_str()), ""_sv);
  e += "";
  CHECK(e.empty());
  e.append(String());
  CHECK(e.empty());
  e.resize(0);
  CHECK(e.empty());
}

YASL_TEST(string_edge_at_oob) {
  String s = "hello";
  CHECK_EQ(s.at(0), 'h');
  CHECK_EQ(s.at(4), 'o');
  CHECK_THROWS(s.at(5), OutOfRange);
  CHECK_THROWS(s.at(100), OutOfRange);
  String empty;
  CHECK_THROWS(empty.at(0), OutOfRange);
}

YASL_TEST(string_edge_sso_boundary) {
  mem::Tracking t;
  {
    size_t cap = String::inline_capacity;
    String s(cap, 'x', &t);
    CHECK_EQ(s.size(), cap);
    CHECK_EQ(t.stats().live(), size_t(0));

    s.push_back('y');
    CHECK_EQ(s.size(), cap + 1);
    CHECK_EQ(t.stats().live(), size_t(1));

    s.pop_back();
    CHECK_EQ(s.size(), cap);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(string_edge_erase_first_and_last_char) {
  String s = "hello";
  s.erase(0, 1);
  CHECK_EQ(s, "ello");
  s.erase(s.size() - 1, 1);
  CHECK_EQ(s, "ell");
}

YASL_TEST(string_edge_erase_entire_content) {
  String s = "goodbye";
  s.erase(0, s.size());
  CHECK(s.empty());
}

YASL_TEST(string_edge_insert_at_boundaries) {
  String s = "world";
  s.insert(0, "hello ");
  CHECK_EQ(s, "hello world");
  s.insert(s.size(), "!");
  CHECK_EQ(s, "hello world!");
}

YASL_TEST(string_edge_replace_entire) {
  String s = "old content";
  s.replace(0, s.size(), "new");
  CHECK_EQ(s, "new");
}

YASL_TEST(string_edge_replace_with_empty) {
  String s = "abcdef";
  s.replace(2, 3, "");
  CHECK_EQ(s, "abf");
}

YASL_TEST(string_edge_resize_shrink_to_zero) {
  String s = "hello";
  s.resize(0);
  CHECK(s.empty());
  CHECK_EQ(StringView(s.c_str()), ""_sv);
}

YASL_TEST(string_edge_resize_grow_with_char) {
  String s = "ab";
  s.resize(5, 'z');
  CHECK_EQ(s, "abzzz");
}

YASL_TEST(string_edge_append_empty) {
  String s = "abc";
  s += "";
  CHECK_EQ(s, "abc");
  s.append(0, 'x');
  CHECK_EQ(s, "abc");
}

YASL_TEST(string_edge_substr_full_and_past_end) {
  String s = "hello";
  CHECK_EQ(s.substr(0), "hello");
  CHECK_EQ(s.substr(5), "");
  CHECK_EQ(s.substr(100), "");
  CHECK_EQ(s.substr(2, 100), "llo");
  CHECK_EQ(s.substr(0, 0), "");
}

YASL_TEST(string_edge_find_not_found) {
  String s = "abcdef";
  CHECK_EQ(s.find("xyz"), String::npos);
  CHECK_EQ(s.find('z'), String::npos);
  String empty;
  CHECK_EQ(empty.find('a'), String::npos);
}

YASL_TEST(string_edge_find_empty_needle) {
  String s = "hello";
  CHECK_EQ(s.find(""), size_t(0));
}

YASL_TEST(string_edge_concat_empty_strings) {
  String a;
  String b;
  CHECK_EQ(a + b, "");
  CHECK_EQ(a + "x", "x");
  CHECK_EQ("x" + b, "x");
}

YASL_TEST(string_edge_compare_different_lengths) {
  String shorter = "abc";
  String longer = "abcd";
  CHECK(shorter < longer);
  CHECK(longer > shorter);
  CHECK(shorter != longer);
  CHECK_EQ(String("abc"), String("abc"));
}

YASL_TEST(string_edge_cow_multiple_copies) {
  mem::Tracking t;
  {
    String base("a shared heap buffer beyond the sso limit", &t);
    String c1 = base;
    String c2 = base;
    String c3 = base;
    CHECK(base.is_shared());
    CHECK_EQ(base.use_count(), size_t(4));
    c1.set(0, 'X');
    CHECK_EQ(base.use_count(), size_t(3));
    CHECK_EQ(base[0], 'a');
    c2 = String();
    CHECK_EQ(base.use_count(), size_t(2));
    c3.clear();
    CHECK_EQ(base.use_count(), size_t(1));
    CHECK(!base.is_shared());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(string_edge_to_upper_lower_empty) {
  String empty;
  CHECK_EQ(empty.to_upper(), "");
  CHECK_EQ(empty.to_lower(), "");
}

YASL_TEST(string_edge_repeat_zero_one) {
  String s = "abc";
  CHECK_EQ(s.repeat(0), "");
  CHECK_EQ(s.repeat(1), "abc");
}

YASL_TEST(string_edge_replace_all_no_match) {
  String s = "hello world";
  CHECK_EQ(s.replace_all("xyz", "abc"), "hello world");
}

YASL_TEST(string_edge_pop_back_single) {
  String s = "a";
  s.pop_back();
  CHECK(s.empty());
}

YASL_TEST(view_edge_empty) {
  StringView v;
  CHECK(v.empty());
  CHECK_EQ(v.size(), size_t(0));
  CHECK_EQ(v.find('x'), StringView::npos);
  CHECK_EQ(v.find("x"), StringView::npos);
  CHECK_EQ(v.rfind('x'), StringView::npos);
  CHECK(!v.contains('x'));
  CHECK_EQ(v.substr(0), ""_sv);
  CHECK_EQ(v.trim(), ""_sv);
  CHECK(!v.split_once('=').has_value());
  CHECK(!v.parse<int>().has_value());
}

YASL_TEST(view_edge_find_at_boundaries) {
  StringView v = "abcabc";
  CHECK_EQ(v.find('a'), size_t(0));
  CHECK_EQ(v.rfind('c'), size_t(5));
  CHECK_EQ(v.find("abc"), size_t(0));
  CHECK_EQ(v.rfind("abc"), size_t(3));
  CHECK_EQ(v.find('a', 1), size_t(3));
  CHECK_EQ(v.rfind('a', 2), size_t(0));
}

YASL_TEST(view_edge_trim_only_whitespace) {
  CHECK_EQ("   "_sv.trim(), ""_sv);
  CHECK_EQ("\t\n\r "_sv.trim(), ""_sv);
}

YASL_TEST(view_edge_parse_boundary_values) {
  CHECK_EQ(*StringView("0").parse<int>(), 0);
  CHECK_EQ(*StringView("-0").parse<int>(), 0);
  CHECK_EQ(*StringView("0").parse<uint64>(), uint64(0));
  CHECK(!StringView("").parse<int>().has_value());
  CHECK(!StringView(" 1").parse<int>().has_value());
  CHECK(!StringView("1 ").parse<int>().has_value());
}

YASL_TEST(builder_edge_empty) {
  StringBuilder b;
  CHECK(b.empty());
  CHECK_EQ(b.size(), size_t(0));
  String s = b.take();
  CHECK(s.empty());
}

YASL_TEST(builder_edge_take_then_reuse) {
  StringBuilder b;
  b << "first";
  String s1 = b.take();
  CHECK(b.empty());
  b << "second";
  String s2 = b.take();
  CHECK_EQ(s1, "first");
  CHECK_EQ(s2, "second");
}

YASL_TEST(builder_edge_format_zero_padding) {
  StringBuilder b;
  b.format("{:05}", 7);
  CHECK_EQ(b.view(), "00007"_sv);
  b.clear();
  b.format("{:03}", 1000);
  CHECK_EQ(b.view(), "1000"_sv);
}
