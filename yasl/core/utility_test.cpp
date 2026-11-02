#include <yasl/core/utility.h>
#include <yasl/core/traits.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(utility_move_and_forward) {
  int x = 42;
  int&& r = yasl::move(x);
  CHECK_EQ(r, 42);

  int y = 7;
  auto&& f = yasl::forward<int>(y);
  CHECK_EQ(f, 7);

  // forward preserves lvalue/rvalue-ness.
  int src = 1;
  int dst = 0;
  dst = yasl::move(src);
  CHECK_EQ(dst, 1);
}

YASL_TEST(utility_exchange) {
  int x = 10;
  int old = yasl::exchange(x, 20);
  CHECK_EQ(old, 10);
  CHECK_EQ(x, 20);
}

YASL_TEST(utility_swap) {
  int a = 1, b = 2;
  yasl::swap(a, b);
  CHECK_EQ(a, 2);
  CHECK_EQ(b, 1);

  String s1 = "abc";
  String s2 = "def";
  yasl::swap(s1, s2);
  CHECK_EQ(s1, "def");
  CHECK_EQ(s2, "abc");
}

YASL_TEST(utility_addressof) {
  int x = 3;
  CHECK_EQ(yasl::addressof(x), &x);
}

YASL_TEST(utility_bit_cast) {
  float f = 1.0f;
  uint32 bits = yasl::bit_cast<uint32>(f);
  CHECK(bits != 0);
  float back = yasl::bit_cast<float>(bits);
  CHECK_EQ(back, f);
}

YASL_TEST(utility_min_max_clamp) {
  CHECK_EQ(yasl::min(3, 5), 3);
  CHECK_EQ(yasl::max(3, 5), 5);
  CHECK_EQ(yasl::clamp(7, 1, 10), 7);
  CHECK_EQ(yasl::clamp(0, 1, 10), 1);
  CHECK_EQ(yasl::clamp(20, 1, 10), 10);
}

YASL_TEST(utility_construct_at_and_destroy_at) {
  alignas(int) unsigned char buf[sizeof(int)];
  int* p = yasl::construct_at(reinterpret_cast<int*>(buf), 99);
  CHECK_EQ(*p, 99);
  yasl::destroy_at(p);
}

YASL_TEST(utility_destroy_n_and_destroy) {
  alignas(int) unsigned char buf[sizeof(int) * 3];
  int* p = reinterpret_cast<int*>(buf);
  yasl::construct_at(p, 1);
  yasl::construct_at(p + 1, 2);
  yasl::construct_at(p + 2, 3);
  yasl::destroy(p, p + 3);
}

YASL_TEST(utility_mem_ops) {
  char src[] = "hello";
  char dst[6] = {};
  yasl::mem_copy(dst, src, 6);
  CHECK_EQ(StringView(dst), StringView("hello"));

  char mv[6] = "world";
  yasl::mem_move(mv + 2, mv, 3);
  CHECK_EQ(StringView(mv, 5), StringView("wowor"));

  char ms[6] = {};
  yasl::mem_set(ms, '#', 5);
  ms[5] = '\0';
  CHECK_EQ(StringView(ms), StringView("#####"));

  CHECK(yasl::mem_compare("abc", "abd", 3) < 0);

  const void* found = yasl::mem_find("hello", 'l', 5);
  CHECK(found != nullptr);
}

YASL_TEST(utility_str_length) {
  CHECK_EQ(yasl::str_length(""), size_t(0));
  CHECK_EQ(yasl::str_length("hello"), size_t(5));
}

YASL_TEST(utility_max_min_value) {
  CHECK_EQ(yasl::max_value<int>(), 2147483647);
  CHECK_EQ(yasl::min_value<int>(), -2147483647 - 1);
  CHECK_EQ(yasl::max_value<uint32>(), uint32(0xFFFFFFFFull));
  CHECK_EQ(yasl::min_value<uint32>(), uint32(0));
}

YASL_TEST(utility_pair) {
  Pair<int, String> p{1, "one"};
  CHECK_EQ(p.first, 1);
  CHECK_EQ(p.second, "one");
  Pair<int, String> q{1, "one"};
  CHECK(p == q);
}

YASL_TEST(utility_less_greater_identity) {
  Less less;
  Greater greater;
  Identity id;
  CHECK(less(1, 2));
  CHECK(!less(2, 1));
  CHECK(greater(2, 1));
  CHECK(!greater(1, 2));
  CHECK_EQ(id(42), 42);
}
