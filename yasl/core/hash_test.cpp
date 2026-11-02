#include <yasl/core/hash.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(hash_bytes_deterministic) {
  const char* text = "hello world";
  uint64 a = hash_bytes(text, 11);
  uint64 b = hash_bytes(text, 11);
  CHECK_EQ(a, b);
}

YASL_TEST(hash_bytes_empty) {
  CHECK_EQ(hash_bytes("", 0), hash_bytes("", 0));
}

YASL_TEST(hash_bytes_avalanche_different_inputs) {
  uint64 h1 = hash_bytes("a", 1);
  uint64 h2 = hash_bytes("b", 1);
  CHECK(h1 != h2);
}

YASL_TEST(hash_bytes_seed_changes_result) {
  uint64 h1 = hash_bytes("abc", 3, 0);
  uint64 h2 = hash_bytes("abc", 3, 1);
  CHECK(h1 != h2);
}

YASL_TEST(hash_bytes_length_extension) {
  // Different lengths with same prefix differ.
  uint64 h1 = hash_bytes("ab", 2);
  uint64 h2 = hash_bytes("abc", 3);
  CHECK(h1 != h2);
}

YASL_TEST(mix64_is_permutation_like) {
  uint64 a = mix64(0);
  uint64 b = mix64(1);
  CHECK(a != b);
  // Zero is a valid result; only the permutation property matters here.
}

YASL_TEST(hash_functor_integers_pass_through) {
  Hash<int> h;
  CHECK_EQ(h(42), uint64(42));
  CHECK_EQ(h(-1), uint64(-1));
}

YASL_TEST(hash_functor_pointers_mixed) {
  Hash<int*> h;
  int x = 1, y = 2;
  CHECK(h(&x) != h(&y));
}

YASL_TEST(hash_string_literal_matches_stringview) {
  Hash<char[4]> h;
  uint64 lit = h("abc");
  uint64 sv = hash_value(StringView("abc", 3));
  CHECK_EQ(lit, sv);
}

YASL_TEST(hash_any_transparent) {
  HashAny ha;
  uint64 a = ha(42);
  uint64 b = ha(42);
  CHECK_EQ(a, b);
  uint64 c = ha(String("hello"));
  uint64 d = ha(StringView("hello"));
  CHECK_EQ(c, d);  // String and StringView hash alike
}

YASL_TEST(hash_quality_spread) {
  // Hash a range of values and check they are not all identical.
  bool all_same = true;
  uint64 first = hash_bytes("x", 1);
  for (int i = 1; i < 1000; ++i) {
    char buf[8];
    size_t n = num::format_uint(buf, uint64(i));
    if (hash_bytes(buf, n) != first) {
      all_same = false;
      break;
    }
  }
  CHECK(!all_same);
}
