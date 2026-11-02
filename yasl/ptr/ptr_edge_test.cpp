#include <yasl/mem/tracking.h>
#include <yasl/ptr/intrusive.h>
#include <yasl/ptr/shared.h>
#include <yasl/ptr/unique.h>
#include <yasl/test/test.h>
#include "yasl/cont/seq/vector.h"

using namespace yasl;

namespace {
int g_live = 0;

struct Widget : RefCounted<Widget> {
  int value;
  explicit Widget(int v) : value(v) { ++g_live; }
  ~Widget() { --g_live; }
};

struct Base : RefCounted<Base> {
  virtual ~Base() = default;
  virtual int id() const { return 0; }
};

struct Derived : Base {
  int id() const override { return 42; }
};
}  // namespace

// ---------------------------------------------------------------------------
// Shared<T>
// ---------------------------------------------------------------------------

YASL_TEST(shared_edge_null) {
  Shared<int> s;
  CHECK(!s);
  CHECK(s == nullptr);
  CHECK(s.get() == nullptr);
  CHECK_EQ(s.use_count(), size_t(0));
  CHECK_EQ(s.weak_count(), size_t(0));
  CHECK(!s.unique());
  s.reset();
  CHECK(!s);
}

YASL_TEST(shared_edge_self_assign) {
  auto s = make_shared<int>(42);
  Shared<int>& ref = s;
  s = ref;
  CHECK_EQ(*s, 42);
  CHECK_EQ(s.use_count(), size_t(1));
}

YASL_TEST(shared_edge_swap) {
  auto a = make_shared<int>(1);
  auto b = make_shared<int>(2);
  a.swap(b);
  CHECK_EQ(*a, 2);
  CHECK_EQ(*b, 1);
}

YASL_TEST(shared_edge_swap_with_null) {
  auto a = make_shared<int>(5);
  Shared<int> null;
  a.swap(null);
  CHECK(!a);
  CHECK_EQ(*null, 5);
}

YASL_TEST(shared_edge_weak_count) {
  auto s = make_shared<int>(1);
  CHECK_EQ(s.weak_count(), size_t(0));
  {
    Weak<int> w1 = s;
    CHECK_EQ(s.weak_count(), size_t(1));
    {
      Weak<int> w2 = s;
      CHECK_EQ(s.weak_count(), size_t(2));
    }
    CHECK_EQ(s.weak_count(), size_t(1));
  }
  CHECK_EQ(s.weak_count(), size_t(0));
}

YASL_TEST(shared_edge_weak_lock_after_reset) {
  Weak<int> w;
  {
    auto s = make_shared<int>(99);
    w = s;
    CHECK(!w.expired());
    auto locked = w.lock();
    CHECK_EQ(*locked, 99);
  }
  CHECK(w.expired());
  CHECK(!w.lock());
}

YASL_TEST(shared_edge_make_mut_when_unique) {
  mem::Tracking t;
  {
    auto s = make_shared_in<int>(&t, 10);
    size_t allocs_before = t.stats().allocations;
    s.make_mut() = 20;
    CHECK_EQ(t.stats().allocations, allocs_before);
    CHECK_EQ(*s, 20);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(shared_edge_make_mut_when_shared) {
  auto a = make_shared<int>(10);
  auto b = a;
  CHECK_EQ(a.use_count(), size_t(2));
  b.make_mut() = 99;
  CHECK_EQ(*a, 10);
  CHECK_EQ(*b, 99);
  CHECK(a.unique());
  CHECK(b.unique());
}

YASL_TEST(shared_edge_no_leak_many_copies) {
  mem::Tracking t;
  {
    auto s = make_shared_in<String>(&t, "shared string value here");
    Vector<Shared<String>> copies(&t);
    for (int i = 0; i < 100; ++i) {
      copies.push_back(s);
    }
    CHECK_EQ(s.use_count(), size_t(101));
    copies.clear();
    CHECK(s.unique());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(shared_edge_from_unique_null) {
  Unique<int> u;
  auto s = Shared<int>::from(yasl::move(u));
  CHECK(!s);
}

// ---------------------------------------------------------------------------
// Weak<T>
// ---------------------------------------------------------------------------

YASL_TEST(weak_edge_default_is_expired) {
  Weak<int> w;
  CHECK(w.expired());
  CHECK(!w.lock());
  CHECK_EQ(w.use_count(), size_t(0));
}

YASL_TEST(weak_edge_reset) {
  auto s = make_shared<int>(7);
  Weak<int> w = s;
  CHECK(!w.expired());
  w.reset();
  CHECK(w.expired());
}

YASL_TEST(weak_edge_copy_and_move) {
  auto s = make_shared<int>(3);
  Weak<int> w1 = s;
  Weak<int> w2 = w1;
  CHECK(!w2.expired());
  Weak<int> w3 = yasl::move(w1);
  CHECK(w1.expired());
  CHECK(!w3.expired());
}

// ---------------------------------------------------------------------------
// Unique<T>
// ---------------------------------------------------------------------------

YASL_TEST(unique_edge_null_state) {
  Unique<int> u;
  CHECK(!u);
  CHECK(u == nullptr);
  CHECK(u.get() == nullptr);
  u.reset();
  CHECK(!u);
}

YASL_TEST(unique_edge_release) {
  mem::Tracking t;
  {
    auto u = make_unique_in<int>(&t, 42);
    CHECK_EQ(t.stats().live(), size_t(1));
    auto [ptr, res] = u.release();
    CHECK(!u);
    CHECK_EQ(t.stats().live(), size_t(1));
    res->deallocate(ptr, sizeof(int));
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(unique_edge_swap) {
  auto a = make_unique<int>(1);
  auto b = make_unique<int>(2);
  a.swap(b);
  CHECK_EQ(*a, 2);
  CHECK_EQ(*b, 1);
}

YASL_TEST(unique_edge_move_assign_self_via_ref) {
  auto u = make_unique<int>(5);
  Unique<int>& ref = u;
  u = yasl::move(ref);
  CHECK(u);
  CHECK_EQ(*u, 5);
}

YASL_TEST(unique_edge_no_leak) {
  mem::Tracking t;
  {
    for (int i = 0; i < 100; ++i) {
      auto u = make_unique_in<String>(&t, "a string to track");
      (void)u;
    }
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

// ---------------------------------------------------------------------------
// IntrusivePtr<T>
// ---------------------------------------------------------------------------

YASL_TEST(intrusive_edge_null) {
  IntrusivePtr<Widget> p;
  CHECK(!p);
  CHECK(p == nullptr);
  CHECK(p.get() == nullptr);
  p.reset();
  CHECK(!p);
}

YASL_TEST(intrusive_edge_use_count) {
  g_live = 0;
  auto a = make_intrusive<Widget>(1);
  CHECK_EQ(a->use_count(), size_t(1));
  auto b = a;
  CHECK_EQ(a->use_count(), size_t(2));
  b.reset();
  CHECK_EQ(a->use_count(), size_t(1));
  a.reset();
  CHECK_EQ(g_live, 0);
}

YASL_TEST(intrusive_edge_from_this) {
  auto a = make_intrusive<Widget>(10);
  IntrusivePtr<Widget> b = a->from_this();
  CHECK(a == b);
  CHECK_EQ(a->use_count(), size_t(2));
}

YASL_TEST(intrusive_edge_swap) {
  auto a = make_intrusive<Widget>(1);
  auto b = make_intrusive<Widget>(2);
  a.swap(b);
  CHECK_EQ(a->value, 2);
  CHECK_EQ(b->value, 1);
}

YASL_TEST(intrusive_edge_upcast_use_count) {
  g_live = 0;
  {
    IntrusivePtr<Derived> d = make_intrusive<Derived>();
    CHECK_EQ(d->use_count(), size_t(1));
    IntrusivePtr<Base> b = d;
    CHECK_EQ(b->use_count(), size_t(2));
    CHECK_EQ(b->id(), 42);
    d.reset();
    CHECK_EQ(b->use_count(), size_t(1));
  }
}

YASL_TEST(intrusive_edge_no_leak) {
  mem::Tracking t;
  g_live = 0;
  {
    Vector<IntrusivePtr<Widget>> vec(&t);
    for (int i = 0; i < 50; ++i) {
      vec.push_back(make_intrusive_in<Widget>(&t, i));
    }
    CHECK_EQ(g_live, 50);
    vec.clear();
    CHECK_EQ(g_live, 0);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(intrusive_edge_adopt_retain) {
  g_live = 0;
  auto original = make_intrusive<Widget>(7);
  Widget* raw = original.get();
  IntrusivePtr<Widget> retained = IntrusivePtr<Widget>::retain(raw);
  CHECK_EQ(raw->use_count(), size_t(2));
  retained.reset();
  CHECK_EQ(raw->use_count(), size_t(1));
  CHECK_EQ(g_live, 1);
  original.reset();
  CHECK_EQ(g_live, 0);
}
