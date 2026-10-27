#include <yasl/algo/algo.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <algorithm>
#include <vector>

using namespace yasl;

namespace {
struct Rng {
  uint64 s = 0x9e3779b97f4a7c15ull;
  uint64 next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  }
};

std::vector<int> make(size_t n, int mod, Rng& r) {
  std::vector<int> v(n);
  for (auto& x : v) {
    x = int(r.next() % uint64(mod));
  }
  return v;
}
}  // namespace

YASL_TEST(algo_sort_random) {
  Rng r;
  for (int mod : {2, 10, 1000, 1 << 30}) {
    auto a = make(100000, mod, r);
    auto b = a;
    yasl::sort(a.data(), a.data() + a.size());
    std::sort(b.begin(), b.end());
    CHECK(a == b);
  }
}

YASL_TEST(algo_sort_shapes) {
  for (size_t n : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(16),
                   size_t(17), size_t(100)}) {
    std::vector<int> asc(n), desc(n), same(n, 7);
    for (size_t i = 0; i < n; ++i) {
      asc[i] = int(i);
      desc[i] = int(n - i);
    }
    for (auto* v : {&asc, &desc, &same}) {
      auto ref = *v;
      yasl::sort(v->data(), v->data() + n);
      std::sort(ref.begin(), ref.end());
      CHECK(*v == ref);
    }
  }
  std::vector<int> v(5000);
  for (size_t i = 0; i < v.size(); ++i) {
    v[i] = int(i < v.size() / 2 ? i : v.size() - i);
  }
  auto ref = v;
  yasl::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
  std::sort(ref.begin(), ref.end(), std::greater<int>());
  CHECK(v == ref);
}

YASL_TEST(algo_heap_fallback) {
  Rng r;
  auto a = make(5000, 1000, r);
  auto b = a;
  Less cmp;
  detail::heap_sort(a.begin(), a.end(), cmp);
  std::sort(b.begin(), b.end());
  CHECK(a == b);
}

YASL_TEST(algo_stable_sort) {
  Rng r;
  mem::Tracking t;
  std::vector<std::pair<int, int>> v(100000);
  for (size_t i = 0; i < v.size(); ++i) {
    v[i] = {int(r.next() % 50), int(i)};
  }
  auto ref = v;
  yasl::stable_sort(
      v.begin(), v.end(),
      [](const auto& a, const auto& b) { return a.first < b.first; }, &t);
  std::stable_sort(ref.begin(), ref.end(), [](const auto& a, const auto& b) {
    return a.first < b.first;
  });
  CHECK(v == ref);
  CHECK_EQ(t.stats().live(), size_t(0));
  CHECK_EQ(t.stats().allocations, size_t(1));
}

YASL_TEST(algo_sort_strings_by_key) {
  std::vector<String> v;
  for (const char* s :
       {"pear", "fig", "banana", "kiwi", "apple", "plum", "date", "cherry",
        "lime", "mango", "melon", "orange", "peach", "grape", "lemon", "olive",
        "papaya", "quince", "guava"}) {
    v.push_back(String(s));
  }
  yasl::sort_by_key(v.begin(), v.end(),
                    [](const String& s) { return s.size(); });
  for (size_t i = 1; i < v.size(); ++i) {
    CHECK(v[i - 1].size() <= v[i].size());
  }
}

YASL_TEST(algo_search_select) {
  Rng r;
  auto a = make(10000, 5000, r);
  for (int trial = 0; trial < 20; ++trial) {
    auto v = a;
    size_t k = size_t(r.next() % v.size());
    yasl::nth_element(v.begin(), v.begin() + k, v.end());
    auto ref = a;
    std::sort(ref.begin(), ref.end());
    CHECK_EQ(v[k], ref[k]);
    for (size_t i = 0; i < k; ++i) {
      CHECK(v[i] <= v[k]);
    }
    for (size_t i = k + 1; i < v.size(); ++i) {
      CHECK(v[i] >= v[k]);
    }
  }
  std::vector<int> s = {1, 3, 5, 7, 9};
  CHECK_EQ(*yasl::binary_search(s.begin(), s.end(), 7), size_t(3));
  CHECK(!yasl::binary_search(s.begin(), s.end(), 4));
  CHECK(!yasl::binary_search(s.begin(), s.begin(), 4));
}

YASL_TEST(algo_partition_and_predicates) {
  Rng r;
  auto v = make(1000, 100, r);
  auto mid = yasl::partition(v.begin(), v.end(), [](int x) { return x < 50; });
  for (auto it = v.begin(); it != mid; ++it) {
    CHECK(*it < 50);
  }
  for (auto it = mid; it != v.end(); ++it) {
    CHECK(*it >= 50);
  }
  CHECK_EQ(size_t(mid - v.begin()),
           yasl::count_if(v.begin(), v.end(), [](int x) { return x < 50; }));
  CHECK(
      yasl::any_of(v.begin(), v.end(), [](int x) { return x == 0 || x > 0; }));
  CHECK(yasl::all_of(v.begin(), v.end(), [](int x) { return x >= 0; }));
  CHECK(yasl::none_of(v.begin(), v.end(), [](int x) { return x < 0; }));
  CHECK_EQ(*yasl::min_element(v.begin(), v.end()),
           *std::min_element(v.begin(), v.end()));
  CHECK_EQ(*yasl::max_element(v.begin(), v.end()),
           *std::max_element(v.begin(), v.end()));
  CHECK(yasl::find_if(v.begin(), v.end(), [](int x) { return x > 1000; }) ==
        v.end());
}
