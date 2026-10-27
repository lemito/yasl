#include <yasl/cont/adapter/adapters.h>
#include <yasl/cont/seq/list.h>
#include <yasl/mem/tracking.h>
#include <yasl/ptr/unique.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

using namespace yasl;

template <class S>
static void stack_roundtrip(S& s) {
  CHECK(s.empty());
  CHECK(!s.pop().has_value());
  s.push(1);
  s.push(2);
  s.emplace(3);
  CHECK_EQ(s.size(), 3u);
  CHECK_EQ(s.top(), 3);
  CHECK_EQ(*s.pop(), 3);
  CHECK_EQ(*s.pop(), 2);
  CHECK_EQ(s.top(), 1);
  s.clear();
  CHECK(s.empty());
}

template <class Q>
static void queue_roundtrip(Q& q) {
  CHECK(q.empty());
  CHECK(!q.pop().has_value());
  q.push(1);
  q.push(2);
  q.emplace(3);
  CHECK_EQ(q.size(), 3u);
  CHECK_EQ(q.front(), 1);
  CHECK_EQ(q.back(), 3);
  CHECK_EQ(*q.pop(), 1);
  CHECK_EQ(*q.pop(), 2);
  CHECK_EQ(q.front(), 3);
  q.clear();
  CHECK(q.empty());
}

YASL_TEST(stack_over_vector_deque_list) {
  Stack<int> a;
  Stack<int, Deque<int>> b;
  Stack<int, List<int>> c;
  stack_roundtrip(a);
  stack_roundtrip(b);
  stack_roundtrip(c);
}

YASL_TEST(queue_over_deque_list) {
  Queue<int> a;
  Queue<int, List<int>> b;
  queue_roundtrip(a);
  queue_roundtrip(b);
}

YASL_TEST(adapters_forward_resource) {
  mem::Tracking t;
  {
    Stack<String, List<String>> s(&t);
    Queue<String> q(&t);
    CHECK_EQ(s.resource(), &t);
    CHECK_EQ(q.resource(), &t);
    s.push("a long string that will not be inlined anywhere at all");
    q.push("another long string that will not be inlined anywhere");
    CHECK(t.stats().live() > 0);
    CHECK_EQ(*s.pop(),
             "a long string that will not be inlined anywhere at all");
  }
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(adapters_move_only) {
  mem::Tracking t;
  {
    Queue<Unique<int>> q(&t);
    q.push(make_unique_in<int>(&t, 4));
    Optional<Unique<int>> x = q.pop();
    CHECK_EQ(**x, 4);
  }
  CHECK_EQ(t.stats().live(), 0u);
}
