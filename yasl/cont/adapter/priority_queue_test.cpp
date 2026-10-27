#include <yasl/cont/adapter/priority_queue.h>
#include <yasl/mem/tracking.h>
#include <yasl/ptr/unique.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <algorithm>
#include <queue>
#include <random>
#include <vector>

using namespace yasl;

YASL_TEST(pq_basic) {
  PriorityQueue<int> pq;
  CHECK(pq.empty());
  CHECK(!pq.pop().has_value());
  CHECK(!pq.peek().has_value());
  for (int x : {5, 1, 9, 3, 7}) {
    pq.push(x);
  }
  CHECK_EQ(pq.top(), 9);
  CHECK_EQ(*pq.peek(), 9);
  CHECK_EQ(*pq.pop(), 9);
  CHECK_EQ(*pq.pop(), 7);
  CHECK_EQ(pq.size(), size_t(3));
}

YASL_TEST(pq_min_heap_and_sorted) {
  PriorityQueue<int, Greater> pq{4, 8, 1, 6};
  CHECK_EQ(pq.top(), 1);
  Vector<int> sorted = yasl::move(pq).into_sorted();
  // ascending by Cmp: for Greater that is descending numbers
  CHECK((std::vector<int>(sorted.begin(), sorted.end()) ==
         std::vector<int>{8, 6, 4, 1}));
  PriorityQueue<int> max{4, 8, 1, 6};
  Vector<int> asc = yasl::move(max).into_sorted();
  CHECK((std::vector<int>(asc.begin(), asc.end()) ==
         std::vector<int>{1, 4, 6, 8}));
}

YASL_TEST(pq_random_vs_std) {
  mem::Tracking t;
  {
    PriorityQueue<String> pq(&t);
    std::priority_queue<std::string> ref;
    std::mt19937 rng(3);
    for (int i = 0; i < 50000; ++i) {
      if (rng() % 3 != 0 || ref.empty()) {
        std::string s =
            "k" + std::to_string(rng() % 100000) + "-padding-padding-padding";
        pq.push(String(s.c_str(), &t));
        ref.push(s);
      } else {
        Optional<String> got = pq.pop();
        CHECK(*got == ref.top().c_str());
        ref.pop();
      }
      CHECK_EQ(pq.size(), ref.size());
    }
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(pq_heapify_and_move_only) {
  std::mt19937 rng(8);
  std::vector<int> data(10000);
  for (int& x : data) {
    x = int(rng() % 1000);
  }
  PriorityQueue<int> pq(data.begin(), data.end());
  std::vector<int> out;
  while (auto x = pq.pop()) {
    out.push_back(*x);
  }
  std::sort(data.rbegin(), data.rend());
  CHECK(out == data);

  mem::Tracking t;
  {
    struct ByValue {
      bool operator()(const Unique<int>& a, const Unique<int>& b) const {
        return *a < *b;
      }
    };
    PriorityQueue<Unique<int>, ByValue> mo(&t);
    for (int i = 0; i < 200; ++i) {
      mo.push(make_unique_in<int>(&t, (i * 37) % 101));
    }
    CHECK_EQ(**mo.pop(), 100);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
