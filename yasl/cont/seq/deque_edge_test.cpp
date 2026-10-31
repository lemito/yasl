#include <yasl/algo/algo.h>
#include <yasl/cont/seq/deque.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <deque>
#include <random>

using namespace yasl;

YASL_TEST(deque_edge_empty_operations) {
  Deque<int> d;
  CHECK(d.empty());
  CHECK_EQ(d.size(), size_t(0));
  CHECK_THROWS(d.at(0), OutOfRange);
  size_t n = 0;
  for ([[maybe_unused]] int x : d) {
    ++n;
  }
  CHECK_EQ(n, size_t(0));
}

YASL_TEST(deque_edge_single_element) {
  Deque<int> d;
  d.push_back(42);
  CHECK_EQ(d.front(), 42);
  CHECK_EQ(d.back(), 42);
  CHECK_EQ(d[0], 42);
  CHECK_EQ(d.take_front(), 42);
  CHECK(d.empty());

  d.push_front(7);
  CHECK_EQ(d.take_back(), 7);
  CHECK(d.empty());
}

YASL_TEST(deque_edge_alternating_push_pop) {
  Deque<int> d;
  for (int i = 0; i < 100; ++i) {
    d.push_back(i);
    d.push_front(-i);
  }
  CHECK_EQ(d.size(), size_t(200));
  CHECK_EQ(d.front(), -99);
  CHECK_EQ(d.back(), 99);
  for (int i = 0; i < 100; ++i) {
    d.pop_front();
    d.pop_back();
  }
  CHECK(d.empty());
}

YASL_TEST(deque_edge_insert_at_boundaries) {
  Deque<int> d{1, 2, 3};
  d.insert(d.begin(), 0);
  CHECK_EQ(d.front(), 0);
  CHECK_EQ(d.size(), size_t(4));

  d.insert(d.end(), 4);
  CHECK_EQ(d.back(), 4);
  CHECK_EQ(d.size(), size_t(5));

  for (int i = 0; i < 5; ++i) {
    CHECK_EQ(d[size_t(i)], i);
  }
}

YASL_TEST(deque_edge_erase_at_boundaries) {
  Deque<int> d{10, 20, 30, 40, 50};
  d.erase_at(0);
  CHECK_EQ(d.front(), 20);
  CHECK_EQ(d.size(), size_t(4));

  d.erase_at(d.size() - 1);
  CHECK_EQ(d.back(), 40);
  CHECK_EQ(d.size(), size_t(3));
}

YASL_TEST(deque_edge_erase_down_to_empty) {
  Deque<int> d{1, 2, 3};
  while (!d.empty()) {
    d.erase(d.begin());
  }
  CHECK(d.empty());
  d.push_back(99);
  CHECK_EQ(d.front(), 99);
}

YASL_TEST(deque_edge_ring_wrap_iteration) {
  Deque<int> d;
  for (int i = 0; i < 8; ++i) {
    d.push_back(i);
  }
  for (int i = 0; i < 4; ++i) {
    d.pop_front();
  }
  for (int i = 8; i < 16; ++i) {
    d.push_back(i);
  }
  CHECK_EQ(d.size(), size_t(12));
  for (size_t i = 0; i < d.size(); ++i) {
    CHECK_EQ(d[i], int(4 + i));
  }
  int prev = -1;
  for (int x : d) {
    CHECK(x > prev);
    prev = x;
  }
}

YASL_TEST(deque_edge_swap) {
  Deque<int> a{1, 2, 3};
  Deque<int> b{10, 20};
  a.swap(b);
  CHECK_EQ(a.size(), size_t(2));
  CHECK_EQ(a.front(), 10);
  CHECK_EQ(b.size(), size_t(3));
  CHECK_EQ(b.front(), 1);
}

YASL_TEST(deque_edge_copy_independence) {
  Deque<int> original{1, 2, 3};
  Deque<int> copy = original;
  copy.push_back(4);
  CHECK_EQ(original.size(), size_t(3));
  CHECK_EQ(copy.size(), size_t(4));
  copy[0] = 99;
  CHECK_EQ(original[0], 1);
}

YASL_TEST(deque_edge_at_oob) {
  Deque<int> d{1, 2, 3};
  CHECK_EQ(d.at(2), 3);
  CHECK_THROWS(d.at(3), OutOfRange);
  CHECK_THROWS(d.at(1000), OutOfRange);
}

YASL_TEST(deque_edge_iterator_arithmetic) {
  Deque<int> d;
  for (int i = 0; i < 20; ++i) {
    d.push_back(i);
  }
  auto it = d.begin();
  it += 5;
  CHECK_EQ(*it, 5);
  it -= 3;
  CHECK_EQ(*it, 2);
  CHECK_EQ(*(it + 10), 12);
  CHECK_EQ(d.end() - d.begin(), ptrdiff_t(20));
  CHECK(d.begin() < d.end());
  CHECK(d.begin() <= d.begin());
}

YASL_TEST(deque_edge_sort_after_wrap) {
  Deque<int> d;
  for (int i = 0; i < 10; ++i) {
    d.push_back(i);
  }
  for (int i = 0; i < 5; ++i) {
    d.pop_front();
  }
  for (int i = 4; i >= 0; --i) {
    d.push_back(i);
  }
  yasl::sort(d.begin(), d.end());
  for (size_t i = 1; i < d.size(); ++i) {
    CHECK(d[i - 1] <= d[i]);
  }
}

YASL_TEST(deque_edge_no_leak_after_clear) {
  mem::Tracking t;
  {
    Deque<String> d(&t);
    for (int i = 0; i < 200; ++i) {
      if (i % 2) {
        d.push_back(String("a long string that goes on the heap", &t));
      } else {
        d.push_front(String("another long string for tracking purposes", &t));
      }
    }
    d.clear();
    CHECK(d.empty());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(deque_vs_std_deque_stress) {
  std::mt19937 rng(0xdeadbeef);
  Deque<int> yd;
  std::deque<int> sd;

  for (int i = 0; i < 200000; ++i) {
    int op = int(rng() % 6);
    int v = int(rng() % 100000);
    if (op < 2) {
      yd.push_back(v);
      sd.push_back(v);
    } else if (op == 2) {
      yd.push_front(v);
      sd.push_front(v);
    } else if (op == 3 && !sd.empty()) {
      CHECK_EQ(yd.take_front(), sd.front());
      sd.pop_front();
    } else if (op == 4 && !sd.empty()) {
      CHECK_EQ(yd.take_back(), sd.back());
      sd.pop_back();
    } else {
      if (!sd.empty()) {
        size_t idx = rng() % sd.size();
        CHECK_EQ(yd[idx], sd[idx]);
      }
    }
    CHECK_EQ(yd.size(), sd.size());
  }

  for (size_t i = 0; i < sd.size(); ++i) {
    CHECK_EQ(yd[i], sd[i]);
  }
}
