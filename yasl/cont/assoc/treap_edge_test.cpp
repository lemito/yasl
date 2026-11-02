#include <yasl/cont/assoc/map.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(treap_edge_empty) {
  TreapMap<int, int> m;
  CHECK(m.empty());
  CHECK_EQ(m.size(), size_t(0));
  CHECK(m.begin() == m.end());
  CHECK(m.find(1) == m.end());
  CHECK(!m.contains(1));
  CHECK_EQ(m.erase(1), size_t(0));
  CHECK(m.lower_bound(1) == m.end());
  CHECK(m.upper_bound(1) == m.end());
  CHECK(m.validate());
  m.clear();
  CHECK(m.empty());
  CHECK(m.validate());
}

YASL_TEST(treap_edge_single_element) {
  TreapMap<int, int> m;
  m[42] = 7;
  CHECK_EQ(m.size(), size_t(1));
  CHECK(!m.empty());
  CHECK_EQ(m.at(42), 7);
  CHECK_EQ(m.front().key, 42);
  CHECK_EQ(m.back().key, 42);
  CHECK_EQ(m.find(42)->value, 7);
  CHECK(m.validate());
  CHECK_EQ(m.erase(42), size_t(1));
  CHECK(m.empty());
  CHECK(m.validate());
}

YASL_TEST(treap_edge_duplicate_insert_no_growth) {
  TreapSet<int> s;
  for (int i = 0; i < 100; ++i) {
    s.insert(7);
  }
  CHECK_EQ(s.size(), size_t(1));
  CHECK_EQ(*s.begin(), 7);
  CHECK(s.validate());
}

YASL_TEST(treap_edge_erase_all_leaves_empty) {
  mem::Tracking t;
  {
    TreapSet<int> s(&t);
    for (int i = 0; i < 5000; ++i) {
      s.insert(i);
    }
    for (int i = 0; i < 5000; ++i) {
      CHECK_EQ(s.erase(i), size_t(1));
    }
    CHECK(s.empty());
    CHECK(s.validate());
    s.insert(1);
    s.insert(2);
    CHECK_EQ(s.size(), size_t(2));
    CHECK(s.validate());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(treap_edge_boundaries_lower_upper) {
  TreapSet<int> s{1, 3, 5, 7, 9};
  CHECK_EQ(*s.lower_bound(0), 1);
  CHECK_EQ(*s.lower_bound(1), 1);
  CHECK_EQ(*s.lower_bound(2), 3);
  CHECK_EQ(*s.upper_bound(1), 3);
  CHECK(s.upper_bound(9) == s.end());
  CHECK(s.lower_bound(10) == s.end());
  CHECK(s.validate());
}

YASL_TEST(treap_edge_erase_min_and_max) {
  TreapSet<int> s{1, 2, 3, 4, 5};
  CHECK_EQ(s.erase(*s.begin()), size_t(1));
  CHECK_EQ(*s.begin(), 2);
  auto last = s.end();
  --last;
  CHECK_EQ(s.erase(*last), size_t(1));
  last = s.end();
  --last;
  CHECK_EQ(*last, 4);
  CHECK_EQ(s.size(), size_t(3));
  CHECK(s.validate());
}

YASL_TEST(treap_edge_split_empty_ranges) {
  TreapSet<int> s;
  for (int i = 0; i < 100; ++i) {
    s.insert(i);
  }
  TreapSet<int> right;
  s.split(200, right);  // nothing >= 200
  CHECK_EQ(s.size(), size_t(100));
  CHECK(right.empty());
  s.join(right);

  TreapSet<int> s2;
  for (int i = 0; i < 100; ++i) {
    s2.insert(i);
  }
  TreapSet<int> right2;
  s2.split(-1, right2);  // everything >= -1
  CHECK(s2.empty());
  CHECK_EQ(right2.size(), size_t(100));
  CHECK(s2.validate() && right2.validate());
  s2.join(right2);
  CHECK_EQ(s2.size(), size_t(100));
}

YASL_TEST(treap_edge_concurrent_lookups_stable) {
  // Treap iterators are stable: erase elsewhere does not invalidate them.
  TreapMap<int, int> m;
  for (int i = 0; i < 1000; ++i) {
    m[i] = i;
  }
  auto it = m.find(42);
  CHECK_EQ(it->key, 42);
  CHECK_EQ(it->value, 42);
  m.erase(100);
  m.insert(2000, 2000);
  CHECK_EQ(it->key, 42);
  CHECK_EQ(it->value, 42);
  CHECK(m.validate());
}
