#include <yasl/cont/assoc/map.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <map>
#include <string>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(treap_map_insert_lookup_erase) {
  TreapMap<String, int> m;
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

YASL_TEST(treap_set_basic) {
  TreapSet<int> s{5, 1, 4, 1, 3};
  CHECK_EQ(s.size(), size_t(4));
  Vector<int> v(s.begin(), s.end());
  CHECK((v == Vector<int>{1, 3, 4, 5}));
  CHECK(s.insert(2).second);
  CHECK(!s.insert(2).second);
  CHECK(s.emplace(10).second);
  CHECK(s.contains(2));
  CHECK_EQ(*s.lower_bound(2), 2);
  CHECK_EQ(*s.upper_bound(2), 3);
  CHECK(s.lower_bound(11) == s.end());
  CHECK_EQ(s.erase(3), size_t(1));
  CHECK_EQ(s.erase(3), size_t(0));
  CHECK_EQ(s.front(), 1);
  CHECK_EQ(s.back(), 10);
  CHECK(s.validate());
}

YASL_TEST(treap_iterators_stable_across_insert) {
  TreapMap<int, int> m;
  for (int i = 0; i < 100; ++i) {
    m[i] = i;
  }
  auto it = m.find(50);
  int* value = &it->value;
  for (int i = 100; i < 10000; ++i) {
    m[i] = i;
  }
  for (int i = 0; i < 50; ++i) {
    m.erase(i);
  }
  CHECK_EQ(it->key, 50);
  CHECK_EQ(value, &m.find(50)->value);
}

YASL_TEST(treap_order_statistics_split_join) {
  mem::Tracking t;
  {
    TreapSet<int> s(&t);
    for (int i = 0; i < 1000; ++i) {
      s.insert(i * 2);
    }
    CHECK_EQ(*s.kth(500), 1000);
    CHECK(s.kth(1000) == s.end());
    CHECK_EQ(s.rank(1001), size_t(501));
    CHECK_EQ(s.index_of(s.find(1998)), size_t(999));

    TreapSet<int> right;
    s.split(600, right);
    CHECK_EQ(s.size(), size_t(300));
    CHECK_EQ(right.front(), 600);
    CHECK(right.resource() == static_cast<mem::Resource*>(&t));
    CHECK(s.validate() && right.validate());
    s.join(right);
    CHECK_EQ(s.size(), size_t(1000));
    CHECK(right.empty() && s.validate());

    TreapSet<int> other(mem::system_resource());
    other.insert(5000);
    s.join(other);
    CHECK_EQ(s.back(), 5000);
    CHECK(s.validate());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(treap_move_only_keys) {
  mem::Tracking t;
  {
    struct MoveOnly {
      int key;
      explicit MoveOnly(int k) : key(k) {}
      MoveOnly(const MoveOnly&) = delete;
      MoveOnly& operator=(const MoveOnly&) = delete;
      MoveOnly(MoveOnly&&) noexcept = default;
      MoveOnly& operator=(MoveOnly&&) noexcept = default;
      bool operator<(const MoveOnly& o) const { return key < o.key; }
    };
    TreapSet<MoveOnly> s(&t);
    for (int i = 0; i < 5000; ++i) {
      CHECK(s.insert(MoveOnly((i * 7919) % 5000)).second);
    }
    CHECK(s.validate());
    CHECK_EQ(s.size(), size_t(5000));
    // erase through iterator returns successor
    auto it = s.find(MoveOnly{100});
    if (it != s.end()) {
      it = s.erase(it);
      CHECK(!s.contains(MoveOnly{100}));
    }
    CHECK(s.validate());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(treap_move_only_values) {
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
    TreapMap<int, MoveOnly> m(&t);
    for (int i = 0; i < 5000; ++i) {
      m.try_emplace(i, i);
    }
    CHECK(m.validate());
    TreapMap<int, MoveOnly> other = yasl::move(m);
    CHECK(other.validate() && m.empty());
    for (auto& [k, v] : other) {
      CHECK(v.p != nullptr);
      (void)k;
    }
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(treap_copy_and_equality) {
  mem::Tracking t;
  {
    TreapMap<int, String> a(&t);
    for (int i = 0; i < 500; ++i) {
      a[i] = String("v", &t);
    }
    TreapMap<int, String> b = a;
    CHECK(a == b);
    b[10] = "changed";
    CHECK(!(a == b));
    a = b;
    CHECK(a == b);
    TreapMap<int, String> c = yasl::move(a);
    CHECK(a.empty());
    CHECK_EQ(c.size(), size_t(500));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
