#include <yasl/algo/algo.h>
#include <yasl/cont/seq/deque.h>
#include <yasl/mem/tracking.h>
#include <yasl/ptr/unique.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <deque>
#include <random>

using namespace yasl;

YASL_TEST(deque_basic) {
  Deque<int> d;
  CHECK(d.empty());
  d.push_back(2);
  d.push_back(3);
  d.push_front(1);
  d.push_front(0);
  CHECK_EQ(d.size(), size_t(4));
  CHECK_EQ(d.front(), 0);
  CHECK_EQ(d.back(), 3);
  for (int i = 0; i < 4; ++i) {
    CHECK_EQ(d[size_t(i)], i);
  }
  CHECK_THROWS(d.at(4), OutOfRange);
  d.pop_front();
  d.pop_back();
  CHECK_EQ(d.size(), size_t(2));
  CHECK_EQ(d.take_front(), 1);
  CHECK_EQ(d.take_back(), 2);
  CHECK(d.empty());
}

YASL_TEST(deque_random_vs_std) {
  mem::Tracking t;
  {
    Deque<String> d(&t);
    std::deque<std::string> ref;
    std::mt19937 rng(5);
    for (int i = 0; i < 100000; ++i) {
      std::string s =
          "item-" + std::to_string(i) + "-with-some-padding-to-leave-sso";
      switch (rng() % 7) {
        case 0:
        case 1:
          d.push_back(String(s.c_str(), &t));
          ref.push_back(s);
          break;
        case 2:
        case 3:
          d.push_front(String(s.c_str(), &t));
          ref.push_front(s);
          break;
        case 4:
          if (!ref.empty()) {
            d.pop_front();
            ref.pop_front();
          }
          break;
        case 5:
          if (!ref.empty()) {
            d.pop_back();
            ref.pop_back();
          }
          break;
        default:
          if (!ref.empty()) {
            size_t at = rng() % ref.size();
            if (rng() % 2) {
              d.erase_at(at);
              ref.erase(ref.begin() + long(at));
            } else {
              d.emplace_at(at, s.c_str(), &t);
              ref.insert(ref.begin() + long(at), s);
            }
          }
      }
      CHECK_EQ(d.size(), ref.size());
      if (i % 997 == 0) {
        for (size_t k = 0; k < ref.size(); ++k) {
          CHECK(d[k] == ref[k].c_str());
        }
      }
    }
    auto copy = d;
    CHECK(copy == d);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(deque_iterators_and_algorithms) {
  Deque<int> d;
  for (int i = 0; i < 20; ++i) {
    d.push_front(i);  // wraps around the ring
  }
  for (int i = 0; i < 10; ++i) {
    d.pop_back();
  }
  for (int i = 100; i < 110; ++i) {
    d.push_back(i);
  }
  yasl::sort(d.begin(), d.end());
  for (size_t i = 1; i < d.size(); ++i) {
    CHECK(d[i - 1] <= d[i]);
  }
  CHECK_EQ(d.end() - d.begin(), ptrdiff_t(20));
  CHECK_EQ(*(d.begin() + 3), d[3]);
  CHECK(d.begin() < d.end());
  long sum = 0;
  for (int x : d) {
    sum += x;
  }
  long expect = 0;
  for (size_t i = 0; i < d.size(); ++i) {
    expect += d[i];
  }
  CHECK_EQ(sum, expect);
  auto it = yasl::lower_bound(d.begin(), d.end(), 100);
  CHECK_EQ(*it, 100);
  d.erase(d.begin());
  CHECK_EQ(d.size(), size_t(19));
  d.insert(d.begin() + 2, -1);
  CHECK_EQ(d[2], -1);
  const Deque<int>& cd = d;
  CHECK_EQ(*cd.begin(), d[0]);
}

YASL_TEST(deque_move_only_no_leak) {
  mem::Tracking t;
  {
    Deque<Unique<int>> d(&t);
    for (int i = 0; i < 1000; ++i) {
      if (i % 2) {
        d.push_back(make_unique_in<int>(&t, i));
      } else {
        d.push_front(make_unique_in<int>(&t, i));
      }
    }
    Deque<Unique<int>> moved = yasl::move(d);
    CHECK(d.empty());
    CHECK_EQ(moved.size(), size_t(1000));
    Unique<int> first = moved.take_front();
    CHECK_EQ(*first, 998);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(deque_self_reference_push) {
  Deque<String> d;
  d.push_back(String("a string longer than the small buffer, for sure....."));
  for (int i = 0; i < 40; ++i) {
    d.push_back(d.front());
    d.push_front(d.back());
  }
  CHECK_EQ(d.size(), size_t(81));
  CHECK(d[0] == d[80]);
}
