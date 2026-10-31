#include <yasl/cont/assoc/trie.h>
#include <yasl/cont/assoc/radix_trie.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

#include <string>
#include <vector>

using namespace yasl;
using namespace yasl::literals;

namespace {
template <class T>
std::vector<std::string> collect_keys(T& t) {
  std::vector<std::string> out;
  t.for_each([&](StringView k, auto&) {
    out.emplace_back(k.data(), k.size());
  });
  return out;
}
}  // namespace

YASL_TEST(trie_edge_empty) {
  Trie<int> t;
  CHECK(t.empty());
  CHECK_EQ(t.size(), size_t(0));
  CHECK(t.find("") == nullptr);
  CHECK(!t.contains(""));
  CHECK(!t.erase(""));
  CHECK(!t.has_prefix(""));
  CHECK_EQ(t.count_with_prefix(""), size_t(0));
  auto m = t.longest_prefix_of("anything");
  CHECK(!m.value);
  CHECK_EQ(m.length, size_t(0));
}

YASL_TEST(trie_edge_empty_key_only) {
  Trie<int> t;
  t[""] = 42;
  CHECK_EQ(t.size(), size_t(1));
  CHECK(t.contains(""));
  CHECK_EQ(*t.find(""), 42);
  CHECK(t.has_prefix(""));
  CHECK_EQ(t.count_with_prefix(""), size_t(1));
  auto m = t.longest_prefix_of("anything");
  CHECK_EQ(*m.value, 42);
  CHECK_EQ(m.length, size_t(0));
  CHECK(t.erase(""));
  CHECK(t.empty());
}

YASL_TEST(trie_edge_prefix_of_key) {
  Trie<int> t;
  t.insert("abc", 1);
  t.insert("ab", 2);
  t.insert("a", 3);
  CHECK_EQ(*t.find("a"), 3);
  CHECK_EQ(*t.find("ab"), 2);
  CHECK_EQ(*t.find("abc"), 1);
  CHECK(t.find("abcd") == nullptr);
  auto m = t.longest_prefix_of("abcde");
  CHECK_EQ(m.length, size_t(3));
  CHECK_EQ(*m.value, 1);
  auto m2 = t.longest_prefix_of("abd");
  CHECK_EQ(m2.length, size_t(2));
  CHECK_EQ(*m2.value, 2);
}

YASL_TEST(trie_edge_erase_all_one_by_one) {
  mem::Tracking r;
  {
    Trie<int> t(&r);
    for (const char* k : {"a", "ab", "abc", "b", "ba"}) {
      t.insert(k, 1);
    }
    for (const char* k : {"a", "ab", "abc", "b", "ba"}) {
      CHECK(t.erase(k));
    }
    CHECK(t.empty());
  }
  CHECK_EQ(r.stats().live(), size_t(0));
}

YASL_TEST(trie_edge_erase_parent_keeps_child) {
  Trie<int> t;
  t.insert("ab", 1);
  t.insert("abc", 2);
  CHECK(t.erase("ab"));
  CHECK(!t.contains("ab"));
  CHECK(t.contains("abc"));
  CHECK(t.has_prefix("ab"));
}

YASL_TEST(trie_edge_for_each_with_prefix_no_match) {
  Trie<int> t;
  t.insert("apple", 1);
  t.insert("apricot", 2);
  size_t n = 0;
  t.for_each_with_prefix("xyz", [&](StringView, int) { ++n; });
  CHECK_EQ(n, size_t(0));
  CHECK_EQ(t.count_with_prefix("xyz"), size_t(0));
  CHECK(!t.has_prefix("xyz"));
}

YASL_TEST(trie_edge_insert_or_assign_updates_value) {
  Trie<int> t;
  CHECK(t.insert("key", 1));
  CHECK_EQ(*t.find("key"), 1);
  CHECK(!t.insert_or_assign("key", 99));
  CHECK_EQ(*t.find("key"), 99);
  CHECK_EQ(t.size(), size_t(1));
}

YASL_TEST(trie_edge_single_byte_keys_all_values) {
  Trie<int> t;
  for (int i = 0; i < 256; ++i) {
    char c = char(i);
    t.insert(StringView(&c, 1), i);
  }
  CHECK_EQ(t.size(), size_t(256));
  for (int i = 0; i < 256; ++i) {
    char c = char(i);
    CHECK_EQ(*t.find(StringView(&c, 1)), i);
  }
  CHECK_EQ(t.count_with_prefix(""), size_t(256));
}

YASL_TEST(trie_edge_for_each_ordered) {
  Trie<int> t;
  for (const char* k : {"z", "a", "m", "b", "aa"}) {
    t.insert(k, 1);
  }
  auto keys = collect_keys(t);
  for (size_t i = 1; i < keys.size(); ++i) {
    CHECK(keys[i - 1] < keys[i]);
  }
}

YASL_TEST(radix_trie_edge_empty) {
  RadixTrie<int> t;
  CHECK(t.empty());
  CHECK(t.find("") == nullptr);
  CHECK(!t.erase(""));
  CHECK(!t.has_prefix(""));
  auto m = t.longest_prefix_of("text");
  CHECK(!m.value);
}

YASL_TEST(radix_trie_edge_single_key_all_prefixes) {
  RadixTrie<int> t;
  t.insert("abcdef", 42);
  CHECK(t.has_prefix(""));
  CHECK(t.has_prefix("a"));
  CHECK(t.has_prefix("abc"));
  CHECK(t.has_prefix("abcdef"));
  CHECK(!t.has_prefix("abcdefg"));
  CHECK(!t.has_prefix("x"));
  CHECK(!t.contains("abc"));
  CHECK(t.contains("abcdef"));
  auto m = t.longest_prefix_of("abcdefghi");
  CHECK_EQ(m.length, size_t(6));
  CHECK_EQ(*m.value, 42);
}

YASL_TEST(radix_trie_edge_erase_parent_keeps_child) {
  RadixTrie<int> t;
  t.insert("ab", 1);
  t.insert("abc", 2);
  CHECK(t.erase("ab"));
  CHECK(!t.contains("ab"));
  CHECK(t.contains("abc"));
  CHECK(t.has_prefix("ab"));
}

YASL_TEST(radix_trie_edge_for_each_ordered) {
  RadixTrie<int> t;
  for (const char* k : {"z", "a", "m", "b", "aa"}) {
    t.insert(k, 1);
  }
  auto keys = collect_keys(t);
  for (size_t i = 1; i < keys.size(); ++i) {
    CHECK(keys[i - 1] < keys[i]);
  }
}

YASL_TEST(trie_vs_radix_trie_equivalent_results) {
  Trie<int> plain;
  RadixTrie<int> radix;

  const char* words[] = {"romane",    "romanus", "romulus", "rubens",
                         "ruber",     "rubicon", "rubicundus", "rom",
                         "rub", "r"};
  int i = 0;
  for (const char* w : words) {
    plain.insert(w, i);
    radix.insert(w, i);
    ++i;
  }
  CHECK_EQ(plain.size(), radix.size());

  std::vector<std::string> plain_keys;
  plain.for_each([&](StringView k, int) {
    plain_keys.emplace_back(k.data(), k.size());
  });
  std::vector<std::string> radix_keys;
  radix.for_each([&](StringView k, int) {
    radix_keys.emplace_back(k.data(), k.size());
  });
  CHECK(plain_keys == radix_keys);

  CHECK_EQ(plain.count_with_prefix("ro"), radix.count_with_prefix("ro"));
  CHECK_EQ(plain.count_with_prefix("ru"), radix.count_with_prefix("ru"));
  CHECK_EQ(plain.count_with_prefix("x"), radix.count_with_prefix("x"));

  for (const char* w : words) {
    CHECK(plain.erase(w) == radix.erase(w));
  }
  CHECK(plain.empty());
  CHECK(radix.empty());
}
