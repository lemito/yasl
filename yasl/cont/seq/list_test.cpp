#include <yasl/cont/seq/list.h>
#include <yasl/mem/tracking.h>
#include <yasl/ptr/unique.h>
#include <yasl/rand/random.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <vector>

using namespace yasl;

namespace {
template <class L>
std::vector<int> items(const L& l) {
  std::vector<int> out;
  for (const auto& x : l) {
    out.push_back(x);
  }
  return out;
}

struct Keyed {
  int key;
  int order;
};
}  // namespace

YASL_TEST(list_basic) {
  List<int> l;
  CHECK(l.empty());
  l.push_back(2);
  l.push_back(3);
  l.push_front(1);
  CHECK_EQ(l.size(), 3u);
  CHECK_EQ(l.front(), 1);
  CHECK_EQ(l.back(), 3);
  CHECK(items(l) == (std::vector<int>{1, 2, 3}));
  l.pop_front();
  l.pop_back();
  CHECK(items(l) == (std::vector<int>{2}));
  l.clear();
  CHECK(l.empty());
  l.push_back(9);
  CHECK_EQ(l.front(), 9);
}

YASL_TEST(list_insert_erase_iterators_survive) {
  List<int> l = {1, 2, 3, 4};
  auto it3 = l.begin();
  ++it3;
  ++it3;
  auto it = l.insert(it3, 10);
  CHECK_EQ(*it, 10);
  auto next = l.erase(it);
  CHECK_EQ(*next, 3);
  CHECK_EQ(*it3, 3);  // untouched iterator is still valid
  CHECK(items(l) == (std::vector<int>{1, 2, 3, 4}));
  l.erase(l.begin(), l.end());
  CHECK(l.empty());
}

YASL_TEST(list_copy_move) {
  mem::Tracking t;
  {
    List<String> a({"x", "y", "z"}, &t);
    List<String> b = a;
    CHECK(a == b);
    List<String> c = yasl::move(a);
    CHECK(a.empty());
    CHECK_EQ(c.size(), 3u);
    CHECK_EQ(c.back(), "z");
    a.push_back("again");
    CHECK_EQ(a.size(), 1u);
    b = c;
    CHECK(b == c);
    c.swap(a);
    CHECK_EQ(c.size(), 1u);
    CHECK_EQ(a.size(), 3u);
  }
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(list_move_only) {
  mem::Tracking t;
  {
    List<Unique<int>> l(&t);
    l.push_back(make_unique_in<int>(&t, 1));
    l.emplace_back(make_unique_in<int>(&t, 2));
    l.push_front(make_unique_in<int>(&t, 0));
    CHECK_EQ(*l.front(), 0);
    CHECK_EQ(*l.back(), 2);
    l.sort([](const Unique<int>& a, const Unique<int>& b) { return *a > *b; });
    CHECK_EQ(*l.front(), 2);
    CHECK(t.stats().live() > 0);
  }
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(list_splice) {
  List<int> a = {1, 2, 3};
  List<int> b = {10, 20, 30};
  auto it20 = ++b.begin();
  a.splice(++a.begin(), b, it20);
  CHECK(items(a) == (std::vector<int>{1, 20, 2, 3}));
  CHECK(items(b) == (std::vector<int>{10, 30}));
  CHECK_EQ(*it20, 20);  // iterator follows the element into `a`
  CHECK_EQ(a.size(), 4u);
  CHECK_EQ(b.size(), 2u);

  a.splice(a.end(), b);
  CHECK(items(a) == (std::vector<int>{1, 20, 2, 3, 10, 30}));
  CHECK(b.empty());
  CHECK_EQ(a.size(), 6u);
  CHECK_EQ(*it20, 20);
  a.erase(it20);
  CHECK_EQ(a.size(), 5u);

  // Reordering inside one list.
  a.splice(a.begin(), a, --a.end());
  CHECK_EQ(a.front(), 30);
  CHECK_EQ(a.size(), 5u);
}

YASL_TEST(list_reverse_remove_unique) {
  List<int> l = {1, 2, 2, 3, 3, 3, 4};
  CHECK_EQ(l.unique(), 3u);
  CHECK(items(l) == (std::vector<int>{1, 2, 3, 4}));
  l.reverse();
  CHECK(items(l) == (std::vector<int>{4, 3, 2, 1}));
  CHECK_EQ(l.back(), 1);
  CHECK_EQ(l.remove_if([](int x) { return x % 2 == 0; }), 2u);
  CHECK(items(l) == (std::vector<int>{3, 1}));
  CHECK_EQ(l.remove(3), 1u);
  CHECK_EQ(l.size(), 1u);
  List<int> e;
  e.reverse();
  e.sort();
  CHECK_EQ(e.unique(), 0u);
}

YASL_TEST(list_sort_is_stable_and_allocation_free) {
  mem::Tracking t;
  List<Keyed> l(&t);
  Random rng(5);
  for (int i = 0; i < 5000; ++i) {
    l.push_back(Keyed{int(rng.range(0, 50)), i});
  }
  size_t allocs = t.stats().allocations;
  l.sort([](const Keyed& a, const Keyed& b) { return a.key < b.key; });
  CHECK_EQ(t.stats().allocations, allocs);
  CHECK_EQ(l.size(), 5000u);
  const Keyed* prev = nullptr;
  size_t n = 0;
  for (const Keyed& p : l) {
    if (prev) {
      CHECK(prev->key < p.key || (prev->key == p.key && prev->order < p.order));
    }
    prev = &p;
    ++n;
  }
  CHECK_EQ(n, 5000u);
  // The backward links must be right too.
  n = 0;
  for (auto it = l.end(); it != l.begin(); --it) {
    ++n;
  }
  CHECK_EQ(n, 5000u);
}
