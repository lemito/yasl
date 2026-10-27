#include <yasl/cont/seq/intrusive_list.h>
#include <yasl/rand/random.h>
#include <yasl/test/test.h>

#include <vector>

using namespace yasl;

namespace {
struct Job : ListNode {
  explicit Job(int v, int o = 0) : value(v), order(o) {}
  int value;
  int order;
};

struct Item {
  explicit Item(int v) : value(v) {}
  int value;
  ListNode hook;
};

template <class L>
std::vector<int> values(L& l) {
  std::vector<int> out;
  for (auto& x : l) {
    out.push_back(x.value);
  }
  return out;
}
}  // namespace

YASL_TEST(intrusive_list_basic) {
  Job a(1), b(2), c(3);
  IntrusiveList<Job> l;
  CHECK(l.empty());
  l.push_back(b);
  l.push_back(c);
  l.push_front(a);
  CHECK_EQ(l.size(), 3u);
  CHECK_EQ(&l.front(), &a);
  CHECK_EQ(&l.back(), &c);
  CHECK(values(l) == (std::vector<int>{1, 2, 3}));
  CHECK(a.linked());
  l.pop_front();
  CHECK(!a.linked());
  l.pop_back();
  CHECK(values(l) == (std::vector<int>{2}));
  l.clear();
  CHECK(l.empty());
  CHECK(!b.linked());
}

YASL_TEST(intrusive_list_auto_unlink_on_destruction) {
  IntrusiveList<Job> l;
  Job a(1);
  l.push_back(a);
  {
    Job temp(2);
    l.push_back(temp);
    CHECK_EQ(l.size(), 2u);
  }  // temp unlinks itself
  CHECK_EQ(l.size(), 1u);
  CHECK(values(l) == (std::vector<int>{1}));

  // The list dying first leaves the elements unlinked, not dangling.
  Job survivor(5);
  {
    IntrusiveList<Job> short_lived;
    short_lived.push_back(survivor);
    CHECK(survivor.linked());
  }
  CHECK(!survivor.linked());
}

YASL_TEST(intrusive_list_member_hook) {
  Item a(1), b(2), c(3);
  IntrusiveList<Item, ListNode, &Item::hook> l;
  l.push_back(a);
  l.push_back(b);
  l.push_back(c);
  CHECK(values(l) == (std::vector<int>{1, 2, 3}));
  CHECK_EQ(&l.front(), &a);
  l.erase(++l.begin());
  CHECK(values(l) == (std::vector<int>{1, 3}));
  CHECK(!decltype(l)::is_linked(b));
  decltype(l)::remove(c);
  CHECK(values(l) == (std::vector<int>{1}));
}

YASL_TEST(intrusive_list_insert_erase_splice) {
  Job a(1), b(2), c(3), d(10), e(20);
  IntrusiveList<Job> l, m;
  l.push_back(a);
  l.push_back(b);
  l.push_back(c);
  m.push_back(d);
  m.push_back(e);

  l.splice(++l.begin(), m, ++m.begin());
  CHECK(values(l) == (std::vector<int>{1, 20, 2, 3}));
  CHECK(values(m) == (std::vector<int>{10}));
  l.splice(l.end(), m);
  CHECK(values(l) == (std::vector<int>{1, 20, 2, 3, 10}));
  CHECK(m.empty());

  auto it = l.erase(l.begin());
  CHECK_EQ(it->value, 20);
  l.reverse();
  CHECK(values(l) == (std::vector<int>{10, 3, 2, 20}));
  CHECK_EQ(l.remove_if([](const Job& j) { return j.value > 5; }), 2u);
  CHECK(values(l) == (std::vector<int>{3, 2}));
}

YASL_TEST(intrusive_list_move_keeps_elements) {
  Job a(1), b(2);
  IntrusiveList<Job> l;
  l.push_back(a);
  l.push_back(b);
  IntrusiveList<Job> m = yasl::move(l);
  CHECK(l.empty());
  CHECK(values(m) == (std::vector<int>{1, 2}));
  m.pop_front();
  CHECK(values(m) == (std::vector<int>{2}));
}

YASL_TEST(intrusive_list_sort_is_stable) {
  Random rng(9);
  std::vector<Job> jobs;
  jobs.reserve(3000);
  for (int i = 0; i < 3000; ++i) {
    jobs.emplace_back(int(rng.range(0, 30)), i);
  }
  IntrusiveList<Job> l;
  for (Job& j : jobs) {
    l.push_back(j);
  }
  l.sort([](const Job& x, const Job& y) { return x.value < y.value; });
  CHECK_EQ(l.size(), 3000u);
  const Job* prev = nullptr;
  for (const Job& j : l) {
    if (prev) {
      CHECK(prev->value < j.value ||
            (prev->value == j.value && prev->order < j.order));
    }
    prev = &j;
  }
  size_t n = 0;
  for (auto it = l.end(); it != l.begin(); --it) {
    ++n;
  }
  CHECK_EQ(n, 3000u);
  l.clear();  // before `jobs` dies
}
