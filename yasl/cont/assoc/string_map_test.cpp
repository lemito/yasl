#include <yasl/cont/assoc/string_map.h>
#include <yasl/mem/tracking.h>
#include <yasl/rand/random.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <map>
#include <string>
#include <vector>

using namespace yasl;

namespace {
std::string str(StringView s) { return std::string(s.data(), s.size()); }

template <class M>
std::vector<std::string> keys_of(M& m) {
  std::vector<std::string> out;
  for (auto& [key, value] : m) {
    (void)value;
    out.push_back(str(key));
  }
  return out;
}
}  // namespace

YASL_TEST(string_map_basic) {
  StringMap<int> m;
  CHECK(m.empty());
  auto r = m.try_emplace("car", 1);
  CHECK(r.second);
  CHECK_EQ(r.first->value, 1);
  CHECK_EQ(str(r.first->key), "car");
  CHECK(!m.try_emplace("car", 5).second);
  m["cart"] = 2;
  m.insert_or_assign("cat", 3);
  m.insert_or_assign("cat", 4);
  CHECK_EQ(m.size(), 3u);
  CHECK_EQ(*m.get("cat"), 4);
  CHECK(m.get("dog") == nullptr);
  CHECK(m.contains("cart"));
  CHECK_EQ(m.at("car"), 1);
  CHECK_THROWS(m.at("zzz"), OutOfRange);
  CHECK_EQ(m.get_or("zzz", 7), 7);
  CHECK(m.find("ca") == m.end());
  CHECK(m.erase("car"));
  CHECK(!m.erase("car"));
  CHECK_EQ(m.size(), 2u);
  m.clear();
  CHECK(m.empty());
  CHECK(m.begin() == m.end());
}

YASL_TEST(string_map_empty_key_and_order) {
  StringMap<int> m;
  m[""] = 0;
  m["b"] = 2;
  m["a"] = 1;
  m["ab"] = 3;
  m[StringView("\xff", 1)] = 9;
  auto keys = keys_of(m);
  CHECK_EQ(keys.size(), 5u);
  CHECK_EQ(keys[0], "");
  CHECK_EQ(keys[1], "a");
  CHECK_EQ(keys[2], "ab");
  CHECK_EQ(keys[3], "b");
  CHECK_EQ(keys[4], "\xff");
  CHECK_EQ(*m.get(""), 0);
}

YASL_TEST(string_map_mutate_through_iteration) {
  StringMap<int> m = {{"x", 1}, {"y", 2}};
  for (auto& [key, value] : m) {
    (void)key;
    value *= 10;
  }
  CHECK_EQ(*m.get("x"), 10);
  CHECK_EQ(*m.get("y"), 20);
  const StringMap<int>& cm = m;
  int sum = 0;
  for (auto& [key, value] : cm) {
    (void)key;
    sum += value;
  }
  CHECK_EQ(sum, 30);
}

YASL_TEST(string_map_prefix_range) {
  StringMap<int> m = {{"apple", 1},  {"app", 2},   {"application", 3},
                      {"banana", 4}, {"apply", 5}, {"ap", 6}};
  std::vector<std::string> got;
  for (auto& [key, value] : m.with_prefix("app")) {
    (void)value;
    got.push_back(str(key));
  }
  CHECK_EQ(got.size(), 4u);
  CHECK_EQ(got[0], "app");
  CHECK_EQ(got[1], "apple");
  CHECK_EQ(got[2], "application");
  CHECK_EQ(got[3], "apply");

  CHECK_EQ(m.count_with_prefix("app"), 4u);
  CHECK_EQ(m.count_with_prefix("ap"), 5u);
  CHECK(m.has_prefix("ban"));
  size_t n = 0;
  for (auto& e : m.with_prefix("zzz")) {
    (void)e;
    ++n;
  }
  CHECK_EQ(n, 0u);
  // find() yields an iterator that continues through the rest of the map.
  auto it = m.find("apple");
  CHECK(it != m.end());
  ++it;
  CHECK_EQ(str(it->key), "application");
}

YASL_TEST(string_map_iterator_copy) {
  StringMap<int> m = {{"a", 1}, {"b", 2}, {"c", 3}};
  auto it = m.begin();
  ++it;
  auto copy = it;
  ++it;
  CHECK_EQ(str(copy->key), "b");
  CHECK_EQ(str(it->key), "c");
}

YASL_TEST(string_map_prune_after_erase) {
  mem::Tracking t;
  {
    StringMap<int> m(&t);
    m["abc"] = 1;
    m["abd"] = 2;
    m["x"] = 3;
    size_t before = t.stats().live();
    m.erase("abc");
    m.erase("abd");
    CHECK(t.stats().live() < before);
    CHECK(!m.has_prefix("ab"));
    CHECK(m.has_prefix("x"));
    CHECK_EQ(m.size(), 1u);
  }
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(string_map_random_against_std_map) {
  mem::Tracking t;
  {
    StringMap<int> m(&t);
    std::map<std::string, int> ref;
    Random rng(11);
    auto make_key = [&] {
      std::string k;
      size_t len = rng.range(0, 6);
      for (size_t i = 0; i < len; ++i) {
        k.push_back(char('a' + rng.range(0, 4)));
      }
      return k;
    };
    for (int i = 0; i < 20000; ++i) {
      std::string k = make_key();
      StringView kv(k.data(), k.size());
      if (rng.range(0, 3) == 0) {
        CHECK_EQ(m.erase(kv), ref.erase(k) > 0);
      } else {
        m[kv] = i;
        ref[k] = i;
      }
    }
    CHECK_EQ(m.size(), ref.size());
    auto it = ref.begin();
    for (auto& [key, value] : m) {
      CHECK(it != ref.end());
      CHECK_EQ(str(key), it->first);
      CHECK_EQ(value, it->second);
      ++it;
    }
    CHECK(it == ref.end());
    std::string p = "ab";
    size_t expect = 0;
    for (auto& kv : ref) {
      if (kv.first.compare(0, p.size(), p) == 0) {
        ++expect;
      }
    }
    CHECK_EQ(m.count_with_prefix("ab"), expect);
  }
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(string_map_copy_move) {
  mem::Tracking t;
  {
    StringMap<String> a(&t);
    a["k"] = "a long value that needs its own heap block, really";
    StringMap<String> b = a;
    CHECK_EQ(*b.get("k"), *a.get("k"));
    StringMap<String> c = yasl::move(a);
    CHECK_EQ(c.size(), 1u);
    CHECK(a.empty());
  }
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(string_set_basic) {
  StringSet s = {"pear", "apple", "peach"};
  CHECK(!s.insert("pear"));
  CHECK(s.insert("fig"));
  CHECK_EQ(s.size(), 4u);
  CHECK(s.contains("fig"));
  CHECK(s.has_prefix("pe"));
  CHECK_EQ(s.count_with_prefix("pe"), 2u);
  std::vector<std::string> got;
  for (auto& [key, unused] : s) {
    (void)unused;
    got.push_back(str(key));
  }
  CHECK_EQ(got.size(), 4u);
  CHECK_EQ(got[0], "apple");
  CHECK_EQ(got[3], "pear");
  CHECK(s.erase("fig"));
  CHECK(!s.contains("fig"));
}
