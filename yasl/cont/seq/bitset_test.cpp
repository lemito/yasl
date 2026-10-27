#include <yasl/cont/seq/bitset.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

#include <random>
#include <set>

using namespace yasl;

YASL_TEST(bitset_basic) {
  BitSet s;
  CHECK_EQ(s.size(), size_t(0));
  CHECK(!s.test(5));
  s.set(3);
  s.set(70);
  s.set(64);
  CHECK_EQ(s.size(), size_t(71));
  CHECK(s.test(3) && s.test(64) && s.test(70));
  CHECK(!s.test(4) && !s.test(1000));
  CHECK_EQ(s.count(), size_t(3));
  s.reset(64);
  s.flip(0);
  CHECK_EQ(s.count(), size_t(3));
  std::vector<size_t> got;
  for (size_t i : s) {
    got.push_back(i);
  }
  CHECK((got == std::vector<size_t>{0, 3, 70}));
  CHECK_EQ(s.find_first(4), size_t(70));
  CHECK_EQ(s.find_first(71), BitSet::npos);
  s.set(2000, false);  // clearing past the end does not grow
  CHECK_EQ(s.size(), size_t(71));
}

YASL_TEST(bitset_ops_and_resize) {
  BitSet a;
  BitSet b;
  for (size_t i : {1, 5, 9, 100}) {
    a.set(i);
  }
  for (size_t i : {5, 9, 77}) {
    b.set(i);
  }
  BitSet u = a;
  u |= b;
  CHECK_EQ(u.count(), size_t(5));
  BitSet n = a;
  n &= b;
  CHECK_EQ(n.count(), size_t(2));
  BitSet x = a;
  x ^= b;
  CHECK_EQ(x.count(), size_t(3));
  a.resize(8);
  CHECK_EQ(a.count(), size_t(2));  // bits 1 and 5 survive
  a.resize(200);
  CHECK_EQ(a.count(), size_t(2));  // shrunk bits stay gone
  CHECK(BitSet(10) == BitSet(500));
}

YASL_TEST(bitset_random_vs_std) {
  mem::Tracking t;
  {
    BitSet s(&t);
    std::set<size_t> ref;
    std::mt19937 rng(4);
    for (int i = 0; i < 20000; ++i) {
      size_t k = rng() % 5000;
      if (rng() % 3) {
        s.set(k);
        ref.insert(k);
      } else {
        s.reset(k);
        ref.erase(k);
      }
    }
    CHECK_EQ(s.count(), ref.size());
    auto it = ref.begin();
    for (size_t i : s) {
      CHECK_EQ(i, *it);
      ++it;
    }
    CHECK(it == ref.end());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
