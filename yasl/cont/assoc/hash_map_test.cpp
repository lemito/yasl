#include <yasl/cont/assoc/hash_map.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(hash_map_basic) {
  HashMap<String, int> m;
  CHECK(m.empty());
  CHECK(m.find("x") == m.end());
  m["apple"] = 3;
  m["banana"] = 5;
  CHECK(m.insert("cherry", 7).second);
  CHECK(!m.insert("cherry", 8).second);
  CHECK_EQ(m.at("cherry"), 7);
  m.insert_or_assign("cherry", 8);
  CHECK_EQ(m.at("cherry"), 8);
  CHECK_EQ(*m.get("apple"_sv), 3);
  CHECK(m.get("durian"_sv) == nullptr);
  CHECK_EQ(m.get_or("durian"_sv, -1), -1);
  CHECK_THROWS(m.at("durian"_sv), OutOfRange);
  CHECK(m.contains("banana"_sv));
  CHECK_EQ(m.erase("banana"_sv), size_t(1));
  CHECK_EQ(m.erase("banana"_sv), size_t(0));
  CHECK_EQ(m.size(), size_t(2));
  int sum = 0;
  for (auto& [k, v] : m) {
    sum += v;
    (void)k;
  }
  CHECK_EQ(sum, 11);
  CHECK(m.validate());
}

YASL_TEST(hash_set_basic) {
  HashSet<int> s{5, 1, 4, 1, 3};
  CHECK_EQ(s.size(), size_t(4));
  CHECK(s.insert(2).second);
  CHECK(!s.insert(2).second);
  CHECK(s.emplace(10).second);
  CHECK(s.contains(10));
  CHECK_EQ(s.erase(3), size_t(1));
  CHECK(!s.contains(3));
  CHECK(s.validate());
}

YASL_TEST(hash_map_random_vs_std) {
  for (int range : {10, 300, 100000}) {
    mem::Tracking t;
    {
      HashMap<int, int> m(&t);
      std::unordered_map<int, int> ref;
      std::mt19937 rng(static_cast<unsigned>(range));
      for (int i = 0; i < 200000; ++i) {
        int k = int(rng() % unsigned(range));
        switch (rng() % 4) {
          case 0:
          case 1:
            m.insert_or_assign(k, i);
            ref[k] = i;
            break;
          case 2:
            CHECK_EQ(m.erase(k), ref.erase(k));
            break;
          default: {
            auto it = ref.find(k);
            const int* v = m.get(k);
            CHECK_EQ(v != nullptr, it != ref.end());
            if (v) {
              CHECK_EQ(*v, it->second);
            }
          }
        }
        if (i % 20000 == 0) {
          CHECK(m.validate());
        }
      }
      CHECK(m.validate());
      CHECK_EQ(m.size(), ref.size());
      size_t seen = 0;
      for (auto& e : m) {
        CHECK_EQ(ref.at(e.key), e.value);
        ++seen;
      }
      CHECK_EQ(seen, ref.size());
      auto copy = m;
      CHECK(copy == m && copy.validate());
      for (auto& [k, v] : ref) {
        CHECK(copy.contains(k));
        CHECK_EQ(copy.erase(k), size_t(1));
        (void)v;
      }
      CHECK(copy.empty() && copy.validate());
    }
    CHECK_EQ(t.stats().live(), size_t(0));
  }
}

YASL_TEST(hash_map_tombstones_reinsert) {
  HashSet<int> s;
  // Fill and drain repeatedly: tombstones must be reclaimed instead of growing
  // forever.
  for (int round = 0; round < 50; ++round) {
    for (int i = 0; i < 1000; ++i) {
      s.insert(round * 100000 + i);
    }
    for (int i = 0; i < 1000; ++i) {
      CHECK_EQ(s.erase(round * 100000 + i), size_t(1));
    }
    CHECK(s.empty());
  }
  CHECK(s.validate());
  CHECK(s.bucket_count() <= 4096);
  for (int i = 0; i < 100; ++i) {
    CHECK(s.insert(i).second);
  }
  for (int i = 0; i < 100; i += 2) {
    s.erase(i);
  }
  for (int i = 0; i < 100; ++i) {
    CHECK_EQ(s.contains(i), i % 2 == 1);
  }
  for (int i = 0; i < 100; i += 2) {
    CHECK(s.insert(i).second);
  }
  CHECK_EQ(s.size(), size_t(100));
  CHECK(s.validate());
}

YASL_TEST(hash_map_erase_if_retain_reserve) {
  mem::Tracking t;
  {
    HashMap<int, String> m(&t);
    m.reserve(1000);
    size_t buckets = m.bucket_count();
    for (int i = 0; i < 1000; ++i) {
      m.try_emplace(i, "value number that is long enough to not be small", &t);
    }
    CHECK_EQ(m.bucket_count(), buckets);
    CHECK(m.load_factor() <= 0.875);
    CHECK_EQ(m.erase_if([](auto& e) { return e.key % 3 == 0; }), size_t(334));
    m.retain([](auto& e) { return e.key % 2 == 0; });
    for (auto& e : m) {
      CHECK(e.key % 2 == 0 && e.key % 3 != 0);
    }
    CHECK(m.validate());
    auto it = m.find(2);
    CHECK(it != m.end());
    it = m.erase(it);
    CHECK(!m.contains(2));
    m.clear();
    CHECK(m.empty() && m.validate());
    m[1] = String("again", &t);
    CHECK_EQ(m.size(), size_t(1));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(hash_map_move_only_and_big) {
  struct Big {
    char pad[300];
    int v;
  };
  HashMap<int, Big> m;
  for (int i = 0; i < 2000; ++i) {
    m[i].v = i;
  }
  for (int i = 0; i < 2000; ++i) {
    CHECK_EQ(m.at(i).v, i);
  }
  HashMap<String, Vector<int>> vm;
  vm["a"_sv.size() ? String("a") : String("b")].push_back(1);
  CHECK_EQ(vm.at("a"_sv).size(), size_t(1));
  auto moved = yasl::move(vm);
  CHECK(vm.empty());
  CHECK_EQ(moved.size(), size_t(1));
}

YASL_TEST(hash_map_string_keys) {
  mem::Tracking t;
  {
    HashMap<String, int> m(&t);
    for (int i = 0; i < 20000; ++i) {
      m[String(std::to_string(i).c_str(), &t)] = i;
    }
    CHECK(m.validate());
    for (int i = 0; i < 20000; ++i) {
      auto s = std::to_string(i);
      CHECK_EQ(*m.get(StringView(s.data(), s.size())), i);
    }
    CHECK(m.get("nope"_sv) == nullptr);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
