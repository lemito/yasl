#include <yasl/cont/assoc/trie.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

#include <map>
#include <random>
#include <string>
#include <vector>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(trie_basic) {
  Trie<int> t;
  CHECK(t.insert("car", 1));
  CHECK(t.insert("cart", 2));
  CHECK(t.insert("cat", 3));
  CHECK(!t.insert("car", 100));
  CHECK_EQ(*t.find("car"), 1);
  CHECK(t.find("ca") == nullptr);
  CHECK(t.contains("cat"));
  CHECK(!t.contains("dog"));
  CHECK_EQ(t.size(), size_t(3));

  CHECK(!t.insert_or_assign("car", 10));
  CHECK_EQ(*t.find("car"), 10);
  t["dog"] = 4;
  CHECK_EQ(t.size(), size_t(4));

  CHECK(t.has_prefix("ca"));
  CHECK(t.has_prefix(""));
  CHECK(!t.has_prefix("cow"));

  CHECK(t.erase("car"));
  CHECK(!t.erase("car"));
  CHECK(t.contains("cart"));
  CHECK_EQ(t.size(), size_t(3));
}

YASL_TEST(trie_empty_key_and_binary) {
  Trie<int> t;
  t.insert("", 0);
  t.insert(StringView("\0\xff", 2), 1);
  t.insert(StringView("\xff", 1), 2);
  CHECK_EQ(*t.find(""), 0);
  CHECK_EQ(*t.find(StringView("\0\xff", 2)), 1);

  std::vector<std::string> keys;
  t.for_each([&](StringView k, int) {
    keys.push_back(std::string(k.data(), k.size()));
  });
  CHECK_EQ(keys.size(), size_t(3));
  CHECK_EQ(keys[0], "");
  CHECK_EQ(keys[1], std::string("\0\xff", 2));
  CHECK_EQ(keys[2], "\xff");
}

YASL_TEST(trie_prefix_queries) {
  Trie<int> t{{"apple", 1},
              {"app", 2},
              {"application", 3},
              {"banana", 4},
              {"apply", 5}};
  std::vector<std::string> keys;
  t.for_each_with_prefix("app", [&](StringView k, int& v) {
    keys.push_back(std::string(k.data(), k.size()));
    v *= 10;
  });
  CHECK((keys ==
         std::vector<std::string>{"app", "apple", "application", "apply"}));
  CHECK_EQ(*t.find("apply"), 50);
  CHECK_EQ(t.count_with_prefix("appl"), size_t(3));
  CHECK_EQ(t.count_with_prefix("x"), size_t(0));

  auto m = t.longest_prefix_of("applesauce");
  CHECK(bool(m));
  CHECK_EQ(m.length, size_t(5));
  CHECK_EQ(*m.value, 10);
  CHECK(!t.longest_prefix_of("ap"));
  CHECK_EQ(t.longest_prefix_of("appetite").length, size_t(3));

  const Trie<int>& ct = t;
  size_t n = 0;
  ct.for_each([&](StringView, const int&) { ++n; });
  CHECK_EQ(n, size_t(5));
}

YASL_TEST(trie_copy_move) {
  mem::Tracking r;
  {
    Trie<std::string> a(&r);
    a.insert("k1", "v1");
    a.insert("k2", "v2");
    Trie<std::string> b = a;
    b["k1"] = "changed";
    CHECK_EQ(*a.find("k1"), "v1");
    CHECK_EQ(*b.find("k1"), "changed");
    Trie<std::string> c = std::move(a);
    CHECK(a.empty());
    CHECK_EQ(c.size(), size_t(2));
    a = c;
    CHECK_EQ(a.size(), size_t(2));
  }
  CHECK_EQ(r.stats().live(), size_t(0));
}

YASL_TEST(trie_random_vs_std_map) {
  mem::Tracking r;
  {
    std::mt19937 rng(42);
    Trie<int> t(&r);
    std::map<std::string, int> ref;
    auto random_key = [&] {
      std::string s(rng() % 6, ' ');
      for (char& c : s) {
        c = char('a' + rng() % 3);
      }
      return s;
    };
    for (int step = 0; step < 20000; ++step) {
      std::string k = random_key();
      int op = int(rng() % 3);
      if (op == 0) {
        CHECK_EQ(t.insert(k, step), ref.emplace(k, step).second);
      } else if (op == 1) {
        CHECK_EQ(t.erase(k), ref.erase(k) == 1);
      } else {
        const int* v = t.find(k);
        auto it = ref.find(k);
        CHECK_EQ(v != nullptr, it != ref.end());
        if (v) {
          CHECK_EQ(*v, it->second);
        }
      }
      CHECK_EQ(t.size(), ref.size());
    }
    std::vector<std::pair<std::string, int>> got;
    t.for_each([&](StringView k, int v) {
      got.emplace_back(std::string(k.data(), k.size()), v);
    });
    CHECK((got ==
           std::vector<std::pair<std::string, int>>(ref.begin(), ref.end())));

    for (auto& [k, v] : ref) {
      CHECK(t.erase(k));
    }
    CHECK(t.empty());
    // Everything was pruned, including the root.
    CHECK_EQ(r.stats().live(), size_t(0));
  }
  CHECK_EQ(r.stats().live(), size_t(0));
}

YASL_TEST(trie_long_key) {
  Trie<int> t;
  std::string key(200000, 'q');
  t.insert(key, 1);
  CHECK_EQ(*t.find(key), 1);
  size_t n = 0;
  t.for_each([&](StringView k, int) { n += k.size(); });
  CHECK_EQ(n, key.size());
  // Destruction must not recurse 200k levels deep.
}
