#include <yasl/cont/assoc/map.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(btree_edge_empty) {
  BTreeMap<int, int> m;
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

YASL_TEST(btree_edge_single_element) {
  BTreeMap<int, int> m;
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

YASL_TEST(btree_edge_iterator_invalidation_on_mutate) {
  // Every insert/erase invalidates all iterators in a B-tree.
  BTreeSet<int> s;
  for (int i = 0; i < 100; ++i) {
    s.insert(i);
  }
  auto it = s.find(50);
  s.insert(200);  // may split nodes
  CHECK(!(it != s.end() && *it == 50) || s.find(50) != s.end());
  // Re-obtain iterators after mutation.
  CHECK(s.contains(50));
  CHECK(s.contains(200));
  CHECK(s.validate());
}

YASL_TEST(btree_edge_erase_all_leaves_empty) {
  mem::Tracking t;
  {
    BTreeSet<int> s(&t);
    for (int i = 0; i < 5000; ++i) {
      s.insert(i);
    }
    for (int i = 0; i < 5000; ++i) {
      CHECK_EQ(s.erase(i), size_t(1));
    }
    CHECK(s.empty());
    CHECK(s.validate());
    // Re-insert after clearing.
    s.insert(1);
    s.insert(2);
    CHECK_EQ(s.size(), size_t(2));
    CHECK(s.validate());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_edge_duplicate_insert_no_growth) {
  BTreeSet<int> s;
  for (int i = 0; i < 100; ++i) {
    s.insert(7);  // same key many times
  }
  CHECK_EQ(s.size(), size_t(1));
  CHECK_EQ(*s.begin(), 7);
  CHECK(s.validate());
}

YASL_TEST(btree_edge_boundaries_lower_upper) {
  BTreeSet<int> s{1, 3, 5, 7, 9};
  CHECK_EQ(*s.lower_bound(0), 1);
  CHECK_EQ(*s.lower_bound(1), 1);
  CHECK_EQ(*s.lower_bound(2), 3);
  CHECK_EQ(*s.upper_bound(1), 3);
  CHECK(s.upper_bound(9) == s.end());
  CHECK(s.lower_bound(10) == s.end());
  CHECK(s.validate());
}

YASL_TEST(btree_edge_erase_min_and_max) {
  BTreeSet<int> s{1, 2, 3, 4, 5};
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

YASL_TEST(btree_edge_string_keys_and_copy) {
  mem::Tracking t;
  {
    BTreeMap<String, int> m(&t);
    for (int i = 0; i < 3000; ++i) {
      m[format("key{}", i)] = i;
    }
    CHECK(m.validate());
    auto copy = m;
    CHECK(copy == m && copy.validate());
    for (int i = 0; i < 3000; i += 2) {
      m.erase(format("key{}", i));
    }
    CHECK(m.validate());
    CHECK_EQ(m.size(), size_t(1500));
    CHECK_EQ(copy.size(), size_t(3000));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_edge_big_elements_rebalance) {
  struct Big {
    int key;
    char pad[200];
    bool operator<(const Big& o) const { return key < o.key; }
    bool operator==(const Big& o) const { return key == o.key; }
  };
  mem::Tracking t;
  {
    BTreeSet<Big> s(&t);
    for (int i = 0; i < 20000; ++i) {
      int k = (i * 7919) % 3000;
      if (i % 3) {
        s.insert(Big{k, {}});
      } else {
        s.erase(Big{k, {}});
      }
    }
    CHECK(s.validate());
    size_t expected = 0;
    for (const Big& x : s) {
      (void)x;
      ++expected;
    }
    CHECK_EQ(s.size(), expected);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
