#include <yasl/cont/assoc/hash_map.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <random>
#include <string>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(hash_map_edge_empty) {
  HashMap<int, int> m;
  CHECK(m.empty());
  CHECK_EQ(m.size(), size_t(0));
  CHECK(m.begin() == m.end());
  CHECK(m.find(0) == m.end());
  CHECK(m.get(0) == nullptr);
  CHECK_EQ(m.get_or(0, 99), 99);
  CHECK_EQ(m.erase(0), size_t(0));
  CHECK(!m.contains(0));
  CHECK_THROWS(m.at(0), OutOfRange);
  m.clear();
  CHECK(m.empty());
}

YASL_TEST(hash_map_edge_single_element) {
  HashMap<int, int> m;
  m[42] = 100;
  CHECK_EQ(m.size(), size_t(1));
  CHECK(m.contains(42));
  CHECK_EQ(*m.get(42), 100);
  CHECK_EQ(m.at(42), 100);
  CHECK_EQ(m.erase(42), size_t(1));
  CHECK(m.empty());
  CHECK(m.validate());
}

YASL_TEST(hash_map_edge_overwrite_same_key) {
  HashMap<int, int> m;
  auto [it1, ins1] = m.insert(7, 1);
  CHECK(ins1);
  CHECK_EQ(it1->value, 1);

  auto [it2, ins2] = m.insert(7, 2);
  CHECK(!ins2);
  CHECK_EQ(it2->value, 1);

  m.insert_or_assign(7, 99);
  CHECK_EQ(m.at(7), 99);
  CHECK_EQ(m.size(), size_t(1));
}

namespace {
struct ConstructCounted {
  static int constructions;
  int v;
  explicit ConstructCounted(int x) : v(x) { ++constructions; }
  ConstructCounted(ConstructCounted&& o) noexcept : v(o.v) { ++constructions; }
  ConstructCounted(const ConstructCounted& o) : v(o.v) { ++constructions; }
};
int ConstructCounted::constructions = 0;
}  // namespace

YASL_TEST(hash_map_edge_try_emplace_no_redundant_construction) {
  ConstructCounted::constructions = 0;
  HashMap<int, ConstructCounted> m;
  m.try_emplace(1, 10);
  int after_first = ConstructCounted::constructions;
  m.try_emplace(1, 20);
  CHECK_EQ(ConstructCounted::constructions, after_first);
  CHECK_EQ(m.at(1).v, 10);
}

YASL_TEST(hash_map_edge_iterator_stability_during_read) {
  HashMap<int, int> m;
  for (int i = 0; i < 100; ++i) {
    m[i] = i;
  }
  size_t seen = 0;
  int sum = 0;
  for (auto& e : m) {
    sum += e.value;
    ++seen;
  }
  CHECK_EQ(seen, size_t(100));
  CHECK_EQ(sum, 99 * 100 / 2);
}

YASL_TEST(hash_map_edge_erase_if_all) {
  HashMap<int, int> m;
  for (int i = 0; i < 200; ++i) {
    m[i] = i;
  }
  size_t removed = m.erase_if([](auto&) { return true; });
  CHECK_EQ(removed, size_t(200));
  CHECK(m.empty());
  CHECK(m.validate());
}

YASL_TEST(hash_map_edge_erase_if_none) {
  HashMap<int, int> m;
  for (int i = 0; i < 50; ++i) {
    m[i] = i;
  }
  size_t removed = m.erase_if([](auto&) { return false; });
  CHECK_EQ(removed, size_t(0));
  CHECK_EQ(m.size(), size_t(50));
  CHECK(m.validate());
}

YASL_TEST(hash_map_edge_retain_all_and_none) {
  HashMap<int, int> m;
  for (int i = 0; i < 50; ++i) {
    m[i] = i;
  }
  m.retain([](auto&) { return true; });
  CHECK_EQ(m.size(), size_t(50));

  m.retain([](auto&) { return false; });
  CHECK(m.empty());
  CHECK(m.validate());
}

YASL_TEST(hash_map_edge_zero_and_negative_keys) {
  HashMap<int, int> m;
  m[0] = 1;
  m[-1] = 2;
  m[-1000000] = 3;
  CHECK_EQ(m.at(0), 1);
  CHECK_EQ(m.at(-1), 2);
  CHECK_EQ(m.at(-1000000), 3);
  CHECK(m.validate());
}

YASL_TEST(hash_map_edge_erase_via_iterator) {
  HashMap<int, int> m;
  for (int i = 0; i < 20; ++i) {
    m[i] = i;
  }
  auto it = m.find(10);
  CHECK(it != m.end());
  it = m.erase(it);
  CHECK(!m.contains(10));
  CHECK(m.validate());
  while (!m.empty()) {
    m.erase(m.begin());
  }
  CHECK(m.empty());
  CHECK(m.validate());
}

YASL_TEST(hash_map_edge_stress_alternating_insert_erase) {
  mem::Tracking t;
  {
    HashMap<int, int> m(&t);
    std::mt19937 rng(12345);
    for (int round = 0; round < 20; ++round) {
      for (int i = 0; i < 500; ++i) {
        m[int(rng() % 1000)] = i;
      }
      for (int i = 0; i < 250; ++i) {
        m.erase(int(rng() % 1000));
      }
      CHECK(m.validate());
    }
    m.clear();
    CHECK(m.empty() && m.validate());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(hash_set_edge_insert_duplicate_boundary) {
  HashSet<int> s;
  for (int i = 0; i < 1000; ++i) {
    CHECK(s.insert(i).second);
  }
  for (int i = 0; i < 1000; ++i) {
    CHECK(!s.insert(i).second);
  }
  CHECK_EQ(s.size(), size_t(1000));
  CHECK(s.validate());
}

YASL_TEST(hash_map_edge_move_invalidates_source) {
  HashMap<int, int> a;
  for (int i = 0; i < 100; ++i) {
    a[i] = i;
  }
  HashMap<int, int> b = yasl::move(a);
  CHECK(a.empty());
  CHECK_EQ(b.size(), size_t(100));
  a[1] = 10;
  CHECK_EQ(a.size(), size_t(1));
  CHECK(b.validate());
}

YASL_TEST(hash_map_edge_large_value_no_leak) {
  struct Large {
    char data[512];
    int v;
  };
  mem::Tracking t;
  {
    HashMap<int, Large> m(&t);
    for (int i = 0; i < 500; ++i) {
      m[i].v = i;
    }
    for (int i = 0; i < 500; ++i) {
      CHECK_EQ(m.at(i).v, i);
    }
    for (int i = 0; i < 250; ++i) {
      m.erase(i);
    }
    CHECK(m.validate());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
