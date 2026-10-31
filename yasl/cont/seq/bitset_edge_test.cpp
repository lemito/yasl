#include <yasl/cont/seq/bitset.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(bitset_edge_empty) {
  BitSet s;
  CHECK_EQ(s.size(), size_t(0));
  CHECK_EQ(s.count(), size_t(0));
  CHECK(!s.any());
  CHECK(!s.test(0));
  CHECK(!s.test(1000));
  CHECK_EQ(s.find_first(), BitSet::npos);
  s.clear();
  CHECK_EQ(s.size(), size_t(0));
  size_t n = 0;
  for ([[maybe_unused]] size_t i : s) {
    ++n;
  }
  CHECK_EQ(n, size_t(0));
  BitSet other;
  CHECK(s == other);
}

YASL_TEST(bitset_edge_single_bit) {
  BitSet s;
  s.set(0);
  CHECK_EQ(s.count(), size_t(1));
  CHECK(s.any());
  CHECK(s.test(0));
  Vector<size_t> bits;
  for (size_t i : s) {
    bits.push_back(i);
  }
  CHECK_EQ(bits.size(), size_t(1));
  CHECK_EQ(bits[0], size_t(0));
  s.reset(0);
  CHECK(!s.any());
}

YASL_TEST(bitset_edge_word_boundary) {
  BitSet s;
  s.set(63);
  s.set(64);
  s.set(127);
  s.set(128);
  CHECK_EQ(s.count(), size_t(4));
  CHECK(s.test(63) && s.test(64) && s.test(127) && s.test(128));
  CHECK(!s.test(62) && !s.test(65) && !s.test(126) && !s.test(129));
  CHECK_EQ(s.find_first(), size_t(63));
  CHECK_EQ(s.find_first(64), size_t(64));
  CHECK_EQ(s.find_first(65), size_t(127));
  CHECK_EQ(s.find_first(128), size_t(128));
  CHECK_EQ(s.find_first(129), BitSet::npos);
}

YASL_TEST(bitset_edge_flip) {
  BitSet s(10);
  s.set(3);
  s.flip(3);
  CHECK(!s.test(3));
  s.flip(3);
  CHECK(s.test(3));
  s.flip(9);
  CHECK(s.test(9));
  CHECK_EQ(s.count(), size_t(2));
}

YASL_TEST(bitset_edge_resize_shrink_clears_bits) {
  BitSet s;
  s.set(5);
  s.set(100);
  s.resize(10);
  CHECK(s.test(5));
  CHECK(!s.test(100));
  CHECK_EQ(s.count(), size_t(1));
  s.resize(200);
  CHECK(s.test(5));
  CHECK(!s.test(100));
}

YASL_TEST(bitset_edge_and_with_different_sizes) {
  BitSet a;
  a.set(1);
  a.set(200);
  BitSet b;
  b.set(1);
  b.set(50);

  BitSet result = a;
  result &= b;
  CHECK_EQ(result.count(), size_t(1));
  CHECK(result.test(1));
  CHECK(!result.test(50));
  CHECK(!result.test(200));
}

YASL_TEST(bitset_edge_or_grows_result) {
  BitSet small;
  small.set(1);
  BitSet big;
  big.set(1000);

  BitSet r = small;
  r |= big;
  CHECK(r.test(1));
  CHECK(r.test(1000));
  CHECK_EQ(r.count(), size_t(2));
}

YASL_TEST(bitset_edge_xor_idempotent) {
  BitSet a;
  a.set(5);
  a.set(10);
  BitSet orig = a;
  a ^= orig;
  CHECK(!a.any());
}

YASL_TEST(bitset_edge_all_bits_set_in_word) {
  BitSet s;
  for (size_t i = 0; i < 64; ++i) {
    s.set(i);
  }
  CHECK_EQ(s.count(), size_t(64));
  CHECK(s.any());
  size_t n = 0;
  for (size_t i : s) {
    CHECK_EQ(i, n);
    ++n;
  }
  CHECK_EQ(n, size_t(64));
  s.resize(32);
  CHECK_EQ(s.count(), size_t(32));
}

YASL_TEST(bitset_edge_find_first_from_past_end) {
  BitSet s;
  s.set(5);
  CHECK_EQ(s.find_first(6), BitSet::npos);
  CHECK_EQ(s.find_first(1000), BitSet::npos);
}

YASL_TEST(bitset_edge_set_past_current_size_grows) {
  BitSet s(4);
  s.set(1000);
  CHECK_EQ(s.size(), size_t(1001));
  CHECK(s.test(1000));
  CHECK(!s.test(999));
}

YASL_TEST(bitset_edge_equality_different_trailing_zeros) {
  BitSet a(100);
  BitSet b(200);
  CHECK(a == b);
  a.set(50);
  b.set(50);
  CHECK(a == b);
  b.set(150);
  CHECK(!(a == b));
}

YASL_TEST(bitset_edge_no_leak) {
  mem::Tracking t;
  {
    BitSet s(&t);
    for (size_t i = 0; i < 10000; i += 7) {
      s.set(i);
    }
    CHECK_EQ(s.count(), size_t(1429));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
