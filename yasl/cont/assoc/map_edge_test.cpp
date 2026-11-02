#include <yasl/cont/assoc/map.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <string>
#include <vector>

using namespace yasl;
using namespace yasl::literals;

namespace {

template <template <class, class, class> class M>
void map_edge_empty() {
  M<int, int, Less> m;
  CHECK(m.empty());
  CHECK_EQ(m.size(), size_t(0));
  CHECK(m.begin() == m.end());
  CHECK(m.find(0) == m.end());
  CHECK(!m.contains(0));
  CHECK(m.get(0) == nullptr);
  CHECK_EQ(m.get_or(0, 99), 99);
  CHECK_THROWS(m.at(0), OutOfRange);
  CHECK_EQ(m.erase(0), size_t(0));
  m.clear();
  CHECK(m.empty());
  CHECK(m.validate());
}

template <template <class, class, class> class M>
void map_edge_single() {
  M<int, String, Less> m;
  m[42] = "hello";
  CHECK_EQ(m.size(), size_t(1));
  CHECK_EQ(m.front().key, 42);
  CHECK_EQ(m.back().key, 42);
  CHECK_EQ(m.at(42), "hello");
  auto it = m.begin();
  CHECK_EQ(it->key, 42);
  ++it;
  CHECK(it == m.end());
  CHECK_EQ(m.erase(42), size_t(1));
  CHECK(m.empty());
  CHECK(m.validate());
}

template <template <class, class, class> class M>
void map_edge_duplicate_insert() {
  M<int, int, Less> m;
  auto [it1, ok1] = m.insert(5, 10);
  CHECK(ok1);
  CHECK_EQ(it1->value, 10);
  auto [it2, ok2] = m.insert(5, 20);
  CHECK(!ok2);
  CHECK_EQ(it2->value, 10);
  m.insert_or_assign(5, 99);
  CHECK_EQ(m.at(5), 99);
  CHECK_EQ(m.size(), size_t(1));
}

template <template <class, class, class> class M>
void map_edge_lower_upper_bound_extremes() {
  M<int, int, Less> m;
  for (int i = 0; i < 10; ++i) {
    m[i * 2] = i;
  }
  auto lb = m.lower_bound(-100);
  CHECK(lb == m.begin());
  auto ub = m.upper_bound(100);
  CHECK(ub == m.end());
  auto lb_exact = m.lower_bound(4);
  CHECK(lb_exact != m.end());
  CHECK_EQ(lb_exact->key, 4);
  auto ub_exact = m.upper_bound(4);
  CHECK(ub_exact != m.end());
  CHECK_EQ(ub_exact->key, 6);
  CHECK(m.validate());
}

template <template <class, class, class> class M>
void map_edge_reverse_iteration() {
  M<int, int, Less> m;
  for (int i = 0; i < 100; ++i) {
    m[i] = i;
  }
  int expected = 99;
  for (auto it = m.end(); it != m.begin();) {
    --it;
    CHECK_EQ(it->key, expected--);
  }
  CHECK_EQ(expected, -1);
}

template <template <class, class, class> class M>
void map_edge_erase_by_iterator_chain() {
  M<int, int, Less> m;
  for (int i = 0; i < 10; ++i) {
    m[i] = i;
  }
  auto it = m.begin();
  while (it != m.end()) {
    it = m.erase(it);
  }
  CHECK(m.empty());
  CHECK(m.validate());
}

}  // namespace

YASL_TEST(btree_map_edge_empty) { map_edge_empty<BTreeMap>(); }
YASL_TEST(treap_map_edge_empty) { map_edge_empty<TreapMap>(); }

YASL_TEST(btree_map_edge_single) { map_edge_single<BTreeMap>(); }
YASL_TEST(treap_map_edge_single) { map_edge_single<TreapMap>(); }

YASL_TEST(btree_map_edge_duplicate) { map_edge_duplicate_insert<BTreeMap>(); }
YASL_TEST(treap_map_edge_duplicate) {
  map_edge_duplicate_insert<TreapMap>();
}

YASL_TEST(btree_map_edge_bounds) {
  map_edge_lower_upper_bound_extremes<BTreeMap>();
}
YASL_TEST(treap_map_edge_bounds) {
  map_edge_lower_upper_bound_extremes<TreapMap>();
}

YASL_TEST(btree_map_edge_reverse) { map_edge_reverse_iteration<BTreeMap>(); }
YASL_TEST(treap_map_edge_reverse) { map_edge_reverse_iteration<TreapMap>(); }

YASL_TEST(btree_map_edge_erase_chain) {
  map_edge_erase_by_iterator_chain<BTreeMap>();
}
YASL_TEST(treap_map_edge_erase_chain) {
  map_edge_erase_by_iterator_chain<TreapMap>();
}

YASL_TEST(btree_edge_range_empty_and_past_end) {
  BTreeSet<int> s;
  for (int i = 0; i < 20; ++i) {
    s.insert(i);
  }
  size_t n = 0;
  for ([[maybe_unused]] int x : s.range(200, 300)) {
    ++n;
  }
  CHECK_EQ(n, size_t(0));

  n = 0;
  for ([[maybe_unused]] int x : s.range(5, 5)) {
    ++n;
  }
  CHECK_EQ(n, size_t(0));

  n = 0;
  for (int x : s.range(0, 20)) {
    CHECK_EQ(x, int(n));
    ++n;
  }
  CHECK_EQ(n, size_t(20));
}

YASL_TEST(btree_edge_kth_rank_on_empty) {
  BTreeSet<int> s;
  CHECK(s.kth(0) == s.end());
  CHECK_EQ(s.rank(0), size_t(0));
  CHECK_EQ(s.rank(99), size_t(0));
}

YASL_TEST(btree_edge_kth_all_positions) {
  BTreeSet<int> s;
  for (int i = 0; i < 200; ++i) {
    s.insert(i * 3);
  }
  for (size_t i = 0; i < 200; ++i) {
    auto it = s.kth(i);
    CHECK(it != s.end());
    CHECK_EQ(*it, int(i) * 3);
    CHECK_EQ(s.index_of(it), i);
  }
  CHECK(s.kth(200) == s.end());
}

YASL_TEST(btree_edge_split_join_empty) {
  mem::Tracking t;
  {
    BTreeSet<int> s(&t);
    BTreeSet<int> rest(&t);
    s.split(0, rest);
    CHECK(s.empty());
    CHECK(rest.empty());

    for (int i = 0; i < 10; ++i) {
      s.insert(i);
    }
    BTreeSet<int> right(&t);
    s.split(100, right);
    CHECK_EQ(s.size(), size_t(10));
    CHECK(right.empty());
    CHECK(s.validate());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_edge_from_sorted_sizes) {
  mem::Tracking t;
  {
    for (int n : {0, 1, 2}) {
      Vector<int> v;
      v.reserve(size_t(n));
      for (int i = 0; i < n; ++i) {
        v.push_back(i);
      }
      auto s = BTreeSet<int>::from_sorted(v.begin(), v.end(), &t);
      CHECK(s.validate());
      CHECK_EQ(s.size(), size_t(n));
      int seen = 0;
      for (int x : s) {
        CHECK_EQ(x, seen++);
      }
    }
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(treap_edge_split_join_preserves_all) {
  mem::Tracking t;
  {
    TreapSet<int> s(&t);
    for (int i = 0; i < 500; ++i) {
      s.insert(i);
    }
    TreapSet<int> right;
    s.split(250, right);
    CHECK_EQ(s.size(), size_t(250));
    CHECK_EQ(right.size(), size_t(250));
    CHECK(s.validate() && right.validate());

    s.join(right);
    CHECK_EQ(s.size(), size_t(500));
    CHECK(right.empty());
    CHECK(s.validate());

    int expected = 0;
    for (int x : s) {
      CHECK_EQ(x, expected++);
    }
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_edge_string_keys_order) {
  BTreeMap<String, int, Less> m;
  for (const char* k : {"banana", "apple", "cherry", "date", "elderberry"}) {
    m[String(k)] = int(StringView(k).size());
  }
  CHECK(m.validate());
  const char* prev = nullptr;
  for (auto& [k, v] : m) {
    if (prev) {
      CHECK(StringView(prev) < k.view());
    }
    prev = k.data();
    CHECK_EQ(v, int(k.size()));
  }
}

YASL_TEST(btree_set_edge_lower_bound_past_max) {
  BTreeSet<int> s{10, 20, 30};
  CHECK(s.lower_bound(31) == s.end());
  CHECK(s.upper_bound(30) == s.end());
  CHECK(s.lower_bound(9) == s.begin());
}

YASL_TEST(map_edge_move_only_no_leak) {
  mem::Tracking t;
  {
    struct MoveOnly {
      explicit MoveOnly(int x) : p(new int(x)) {}
      MoveOnly(MoveOnly&& o) noexcept : p(o.p) { o.p = nullptr; }
      MoveOnly& operator=(MoveOnly&& o) noexcept {
        delete p;
        p = o.p;
        o.p = nullptr;
        return *this;
      }
      ~MoveOnly() { delete p; }
      int* p = nullptr;
    };
    BTreeMap<int, MoveOnly> m(&t);
    for (int i = 0; i < 100; ++i) {
      m.try_emplace(i, i);
    }
    for (int i = 0; i < 50; ++i) {
      m.erase(i * 2);
    }
    CHECK(m.validate());
    BTreeMap<int, MoveOnly> other = yasl::move(m);
    CHECK(m.empty());
    CHECK(other.validate());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
