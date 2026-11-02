#include <yasl/cont/assoc/map.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <map>
#include <string>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(btree_map_insert_lookup_erase) {
  BTreeMap<String, int> m;
  m["apple"] = 3;
  m["banana"] = 5;
  CHECK(m.insert("cherry", 7).second);
  CHECK(!m.insert("cherry", 8).second);
  CHECK_EQ(m.at("cherry"), 7);
  m.insert_or_assign("cherry", 8);
  CHECK_EQ(m.at("cherry"), 8);

  CHECK_EQ(*m.get("apple"_sv), 3);
  CHECK(m.get("durian") == nullptr);
  CHECK_EQ(m.get_or("durian", -1), -1);
  CHECK_THROWS(m.at("durian"), OutOfRange);

  Vector<String> keys;
  for (auto& [k, v] : m) {
    keys.push_back(k);
    v += 100;
  }
  CHECK_EQ(keys.size(), size_t(3));
  CHECK_EQ(keys[0], "apple");
  CHECK_EQ(keys[1], "banana");
  CHECK_EQ(keys[2], "cherry");
  CHECK_EQ(m["banana"], 105);

  auto it = m.find("banana");
  it = m.erase(it);
  CHECK_EQ(it->key, "cherry");
  CHECK_EQ(m.size(), size_t(2));
  CHECK_EQ(m.erase("nope"), size_t(0));
  CHECK(m.validate());
}

YASL_TEST(btree_set_basic) {
  BTreeSet<int> s{5, 1, 4, 1, 3};
  CHECK_EQ(s.size(), size_t(4));
  Vector<int> v(s.begin(), s.end());
  CHECK((v == Vector<int>{1, 3, 4, 5}));
  CHECK(s.insert(2).second);
  CHECK(!s.insert(2).second);
  CHECK(s.contains(2));
  CHECK_EQ(*s.lower_bound(2), 2);
  CHECK_EQ(*s.upper_bound(2), 3);
  CHECK(s.lower_bound(11) == s.end());
  CHECK_EQ(s.erase(3), size_t(1));
  CHECK_EQ(s.erase(3), size_t(0));
  CHECK_EQ(s.front(), 1);
  CHECK_EQ(s.back(), 5);
  CHECK(s.validate());
}

YASL_TEST(btree_map_iteration_order) {
  mem::Tracking t;
  {
    BTreeMap<int, String> m(&t);
    for (int i = 0; i < 100; ++i) {
      m[i] = String("v", &t);
    }
    int expected = 0;
    for (const auto& [k, v] : m) {
      CHECK_EQ(k, expected);
      ++expected;
    }
    CHECK_EQ(expected, 100);
    // reverse iteration
    expected = 99;
    for (auto it = m.end(); it != m.begin();) {
      --it;
      CHECK_EQ(it->key, expected);
      --expected;
    }
    CHECK_EQ(expected, -1);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_order_statistics_kth_rank) {
  mem::Tracking t;
  {
    BTreeSet<int> s(&t);
    for (int i = 0; i < 1000; ++i) {
      s.insert(i * 2);
    }
    CHECK_EQ(*s.kth(500), 1000);
    CHECK(s.kth(1000) == s.end());
    CHECK_EQ(s.rank(1001), size_t(501));
    CHECK_EQ(s.index_of(s.find(1998)), size_t(999));
    CHECK(s.validate());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_split_join) {
  mem::Tracking t;
  {
    BTreeSet<int> s(&t);
    for (int i = 0; i < 5000; ++i) {
      s.insert(i * 2);
    }
    BTreeSet<int> right;
    s.split(3001, right);
    CHECK_EQ(s.size(), size_t(1501));
    CHECK_EQ(right.size(), size_t(3499));
    CHECK_EQ(s.back(), 3000);
    CHECK_EQ(right.front(), 3002);
    CHECK(s.validate() && right.validate());
    s.join(right);
    CHECK(right.empty() && right.validate());
    CHECK_EQ(s.size(), size_t(5000));
    CHECK(s.validate());
    CHECK_EQ(*s.kth(2500), 5000);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_move_only_values) {
  mem::Tracking t;
  {
    struct MoveOnly {
      int* p;
      explicit MoveOnly(int x) : p(new int(x)) {}
      MoveOnly(const MoveOnly&) = delete;
      MoveOnly& operator=(const MoveOnly&) = delete;
      MoveOnly(MoveOnly&& o) noexcept : p(o.p) { o.p = nullptr; }
      MoveOnly& operator=(MoveOnly&& o) noexcept {
        delete p;
        p = o.p;
        o.p = nullptr;
        return *this;
      }
      ~MoveOnly() { delete p; }
    };
    BTreeMap<int, MoveOnly> m(&t);
    for (int i = 0; i < 5000; ++i) {
      m.try_emplace((i * 7919) % 5000, i);
    }
    CHECK(m.validate());
    for (int i = 0; i < 5000; i += 3) {
      m.erase(i);
    }
    CHECK(m.validate());
    BTreeMap<int, MoveOnly> other = yasl::move(m);
    CHECK(other.validate() && m.empty());
    for (auto& [k, v] : other) {
      CHECK(v.p != nullptr);
      (void)k;
    }
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(btree_from_sorted_and_range) {
  mem::Tracking t;
  {
    for (int n : {0, 1, 2, 31, 32, 33, 64, 1000}) {
      Vector<int> v(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i) {
        v[size_t(i)] = i * 2;
      }
      auto s = BTreeSet<int>::from_sorted(v.begin(), v.end(), &t);
      CHECK(s.validate());
      CHECK_EQ(s.size(), size_t(n));
      Vector<int> got;
      for (int x : s) {
        got.push_back(x);
      }
      CHECK(got == v);
      if (n > 0) {
        CHECK_EQ(*s.kth(size_t(n) - 1), (n - 1) * 2);
      }
      for (int i = 0; i < n; ++i) {
        CHECK_EQ(s.erase(i * 2), size_t(1));
      }
      CHECK(s.empty() && s.validate());
    }
    BTreeSet<int> s(&t);
    for (int i = 0; i < 100; ++i) {
      s.insert(i);
    }
    int sum = 0;
    for (int x : s.range(10, 20)) {
      sum += x;
    }
    CHECK_EQ(sum, 145);
    CHECK(s.range(200, 300).begin() == s.range(200, 300).end());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
