#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/ptr/intrusive.h>
#include <yasl/test/test.h>

using namespace yasl;

namespace {
int g_live = 0;

struct Node : RefCounted<Node> {
  explicit Node(int v) : value(v) { ++g_live; }
  ~Node() { --g_live; }

  IntrusivePtr<Node> self() { return from_this(); }

  int value;
};

struct Plain : RefCounted<Plain, Counting::Plain> {
  int value = 0;
};

struct Animal : RefCounted<Animal> {
  virtual ~Animal() = default;
  virtual int legs() const = 0;
};

struct Dog : Animal {
  int legs() const override { return 4; }
  char pad[64];
};
}  // namespace

YASL_TEST(intrusive_size) {
  CHECK_EQ(sizeof(IntrusivePtr<Node>), sizeof(void*));
  CHECK_EQ(sizeof(IntrusivePtr<Plain>), sizeof(void*));
}

YASL_TEST(intrusive_basic) {
  mem::Tracking t;
  {
    IntrusivePtr<Node> a = make_intrusive_in<Node>(&t, 5);
    CHECK_EQ(g_live, 1);
    CHECK_EQ(a->value, 5);
    CHECK_EQ(a->use_count(), 1u);
    IntrusivePtr<Node> b = a;
    CHECK_EQ(a->use_count(), 2u);
    IntrusivePtr<Node> c = yasl::move(b);
    CHECK(!b);
    CHECK_EQ(a->use_count(), 2u);
    c.reset();
    CHECK_EQ(a->use_count(), 1u);
    CHECK_EQ(t.stats().live(), 1u);
  }
  CHECK_EQ(g_live, 0);
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(intrusive_from_this_and_adopt_retain) {
  mem::Tracking t;
  {
    IntrusivePtr<Node> a = make_intrusive_in<Node>(&t, 1);
    IntrusivePtr<Node> b = a->self();
    CHECK(a == b);
    CHECK_EQ(a->use_count(), 2u);

    Node* raw = b.release();
    CHECK_EQ(a->use_count(), 2u);
    IntrusivePtr<Node> c = IntrusivePtr<Node>::adopt(raw);
    CHECK_EQ(a->use_count(), 2u);
    IntrusivePtr<Node> d = IntrusivePtr<Node>::retain(raw);
    CHECK_EQ(a->use_count(), 3u);
  }
  CHECK_EQ(t.stats().live(), 0u);
  CHECK_EQ(g_live, 0);
}

YASL_TEST(intrusive_upcast) {
  mem::Tracking t;
  {
    IntrusivePtr<Dog> dog = make_intrusive_in<Dog>(&t);
    IntrusivePtr<Animal> animal = dog;
    CHECK_EQ(animal->legs(), 4);
    dog.reset();
    CHECK_EQ(t.stats().live(), 1u);
  }
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(intrusive_unmanaged_object_is_not_destroyed) {
  Plain stack_object;
  {
    IntrusivePtr<Plain> p = IntrusivePtr<Plain>::retain(&stack_object);
    IntrusivePtr<Plain> q = p;
    CHECK_EQ(stack_object.use_count(), 2u);
  }
  CHECK_EQ(stack_object.use_count(), 0u);
}

YASL_TEST(intrusive_plain_counter) {
  mem::Tracking t;
  {
    IntrusivePtr<Plain> p = make_intrusive_in<Plain>(&t);
    IntrusivePtr<Plain> q = p;
    CHECK_EQ(p->use_count(), 2u);
  }
  CHECK_EQ(t.stats().live(), 0u);
}

YASL_TEST(intrusive_threads) {
  mem::Tracking t;
  {
    IntrusivePtr<Node> shared =
        make_intrusive_in<Node>(mem::system_resource(), 0);
    Vector<Thread> threads;
    for (int i = 0; i < 4; ++i) {
      threads.push_back(Thread([shared] {
        for (int k = 0; k < 20000; ++k) {
          IntrusivePtr<Node> copy = shared;
          IntrusivePtr<Node> again = copy->self();
        }
      }));
    }
    for (auto& th : threads) {
      th.join();
    }
    CHECK_EQ(shared->use_count(), 1u);  // the threads dropped their copies
  }
  CHECK_EQ(g_live, 0);
}
