#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(vector_edge_empty_operations) {
  Vector<int> v;
  size_t count = 0;
  for ([[maybe_unused]] int x : v) {
    ++count;
  }
  CHECK_EQ(count, size_t(0));
  CHECK_PANICS(v.pop());
  CHECK(!v.first().has_value());
  CHECK(!v.last().has_value());
  v.shrink_to_fit();
  CHECK_EQ(v.capacity(), size_t(0));
  v.resize(0);
  CHECK(v.empty());
  Vector<int> w;
  CHECK(v == w);
  CHECK(!(v < w));
}

YASL_TEST(vector_edge_single_element) {
  Vector<int> v;
  v.push_back(42);
  CHECK_EQ(v.size(), size_t(1));
  CHECK_EQ(v.front(), 42);
  CHECK_EQ(v.back(), 42);
  CHECK_EQ(*v.first(), 42);
  CHECK_EQ(*v.last(), 42);
  CHECK_EQ(v.pop(), 42);
  CHECK(v.empty());
}

YASL_TEST(vector_edge_at_oob) {
  Vector<int> v{1, 2, 3};
  CHECK_EQ(v.at(0), 1);
  CHECK_EQ(v.at(2), 3);
  CHECK_THROWS(v.at(3), OutOfRange);
  CHECK_THROWS(v.at(100), OutOfRange);
  Vector<int> e;
  CHECK_THROWS(e.at(0), OutOfRange);
}

YASL_TEST(vector_edge_insert_erase_boundaries) {
  Vector<int> v{10, 20, 30};
  v.insert(v.begin(), 0);
  CHECK_EQ(v[0], 0);
  CHECK_EQ(v.size(), size_t(4));
  v.insert(v.end(), 99);
  CHECK_EQ(v.back(), 99);
  CHECK_EQ(v.size(), size_t(5));
  v.erase(v.begin());
  CHECK_EQ(v.front(), 10);
  v.erase(v.end() - 1);
  CHECK_EQ(v.back(), 30);
  CHECK_EQ(v.size(), size_t(3));
}

YASL_TEST(vector_edge_erase_full_range) {
  mem::Tracking t;
  {
    Vector<String> v(&t);
    for (const char* s : {"a", "b", "c", "d"}) {
      v.push_back(String(s, &t));
    }
    v.erase(v.begin(), v.end());
    CHECK(v.empty());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(vector_edge_erase_empty_range) {
  Vector<int> v{1, 2, 3};
  v.erase(v.begin() + 1, v.begin() + 1);
  CHECK_EQ(v.size(), size_t(3));
  CHECK_EQ(v[1], 2);
}

YASL_TEST(vector_edge_resize_same) {
  Vector<int> v{1, 2, 3};
  v.resize(3);
  CHECK_EQ(v.size(), size_t(3));
  CHECK_EQ(v[2], 3);
  v.resize(3, 99);
  CHECK_EQ(v.size(), size_t(3));
  CHECK_EQ(v[2], 3);
}

YASL_TEST(vector_edge_reserve_below_capacity) {
  Vector<int> v;
  v.reserve(1000);
  CHECK(v.capacity() >= 1000);
  v.reserve(10);
  CHECK(v.capacity() >= 10);
  CHECK(v.empty());
}

YASL_TEST(vector_edge_range_insert_empty) {
  Vector<int> v{1, 2, 3};
  v.insert(v.begin() + 1, {});
  CHECK_EQ(v.size(), size_t(3));
  CHECK_EQ(v[1], 2);
}

YASL_TEST(vector_edge_drain_extremes) {
  mem::Tracking t;
  {
    Vector<String> v(&t);
    for (const char* s : {"alpha", "bravo", "charlie"}) {
      v.push_back(String(s, &t));
    }
    auto empty = v.drain(1, 1);
    CHECK(empty.empty());
    CHECK_EQ(v.size(), size_t(3));
    auto all = v.drain(0, 3);
    CHECK_EQ(all.size(), size_t(3));
    CHECK(v.empty());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(vector_edge_split_off_extremes) {
  mem::Tracking t;
  {
    Vector<String> v(&t);
    for (const char* s : {"a", "b", "c"}) {
      v.push_back(String(s, &t));
    }
    auto all = v.split_off(0);
    CHECK(v.empty());
    CHECK_EQ(all.size(), size_t(3));
    auto none = all.split_off(all.size());
    CHECK_EQ(all.size(), size_t(3));
    CHECK(none.empty());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(vector_edge_windows_chunks_trivial) {
  Vector<int> empty;
  size_t n = 0;
  for ([[maybe_unused]] auto w : empty.windows(1)) {
    ++n;
  }
  CHECK_EQ(n, size_t(0));
  for ([[maybe_unused]] auto c : empty.chunks(1)) {
    ++n;
  }
  CHECK_EQ(n, size_t(0));

  Vector<int> one{7};
  for (auto w : one.windows(1)) {
    CHECK_EQ(w[0], 7);
    ++n;
  }
  CHECK_EQ(n, size_t(1));
  n = 0;
  for (auto c : one.chunks(1)) {
    CHECK_EQ(c[0], 7);
    ++n;
  }
  CHECK_EQ(n, size_t(1));
}

YASL_TEST(vector_edge_dedup_extremes) {
  Vector<int> already_unique{1, 2, 3, 4, 5};
  already_unique.dedup();
  CHECK_EQ(already_unique.size(), size_t(5));

  Vector<int> all_same(10, 7);
  all_same.dedup();
  CHECK_EQ(all_same.size(), size_t(1));
  CHECK_EQ(all_same[0], 7);

  Vector<int> empty;
  empty.dedup();
  CHECK(empty.empty());
}

YASL_TEST(vector_edge_index_of) {
  Vector<int> v{10, 20, 30};
  CHECK_EQ(*v.index_of(10), size_t(0));
  CHECK_EQ(*v.index_of(30), size_t(2));
  CHECK(!v.index_of(99).has_value());
  Vector<int> e;
  CHECK(!e.index_of(1).has_value());
}

YASL_TEST(vector_edge_contains_empty) {
  Vector<int> v;
  CHECK(!v.contains(0));
}

YASL_TEST(vector_edge_sort_two) {
  Vector<int> v{2, 1};
  v.sort();
  CHECK_EQ(v[0], 1);
  CHECK_EQ(v[1], 2);

  Vector<int> already{1, 2};
  already.sort();
  CHECK_EQ(already[0], 1);

  Vector<int> same{5, 5};
  same.sort();
  CHECK_EQ(same[0], 5);
  CHECK_EQ(same[1], 5);
}

YASL_TEST(vector_edge_stable_sort_equal_keys) {
  struct Item {
    int key;
    int original_pos;
  };
  Vector<Item> v{{3, 0}, {1, 1}, {2, 2}, {1, 3}, {1, 4}};
  v.stable_sort([](const Item& a, const Item& b) { return a.key < b.key; });
  CHECK_EQ(v[0].original_pos, 1);
  CHECK_EQ(v[1].original_pos, 3);
  CHECK_EQ(v[2].original_pos, 4);
  CHECK_EQ(v[3].key, 2);
  CHECK_EQ(v[4].key, 3);
}

YASL_TEST(vector_edge_retain_removes_all) {
  mem::Tracking t;
  {
    Vector<String> v(&t);
    for (const char* s : {"x", "y", "z"}) {
      v.push_back(String(s, &t));
    }
    v.retain([](const String&) { return false; });
    CHECK(v.empty());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(vector_edge_destructor_no_leak) {
  mem::Tracking t;
  {
    Vector<String> v(&t);
    for (int i = 0; i < 50; ++i) {
      v.push_back(String("a moderately long string for tracking", &t));
    }
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(vector_edge_self_assign) {
  Vector<int> v{1, 2, 3};
  Vector<int>& alias = v;
  v = alias;
  CHECK_EQ(v.size(), size_t(3));
  CHECK_EQ(v[0], 1);
  CHECK_EQ(v[2], 3);
}
