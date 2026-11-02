#include <yasl/conc/atomic.h>
#include <yasl/conc/condvar.h>
#include <yasl/conc/guarded.h>
#include <yasl/conc/mutex.h>
#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::literals;

// Note on the handshakes below: wait() registers itself and releases the
// mutex before blocking, so locking the mutex from the notifier proves that
// every waiter is already subscribed. A notify issued while holding the lock
// can therefore never be lost.

YASL_TEST(condvar_wait_notify_one) {
  Mutex m;
  CondVar cv;
  Atomic<int> started{0};
  Atomic<int> woken{0};

  Thread waiter([&] {
    LockGuard<Mutex> g(m);
    started.store(1);
    cv.wait(m);  // raw wait, no predicate
    woken.store(1);
  });

  while (started.load() == 0) {
    this_thread::yield();
  }
  {
    LockGuard<Mutex> g(m);  // blocks until the waiter is inside cv.wait()
    cv.notify_one();
  }
  waiter.join();
  CHECK_EQ(woken.load(), 1);
}

YASL_TEST(condvar_notify_all_wakes_all) {
  mem::Tracking t;
  {
    Mutex m;
    CondVar cv;
    Atomic<int> ready{0};
    Atomic<int> released{0};
    constexpr int kWaiters = 4;

    Vector<Thread> threads;
    for (int i = 0; i < kWaiters; ++i) {
      threads.push_back(Thread([&] {
        LockGuard<Mutex> g(m);
        ready.fetch_add(1);
        cv.wait(m);
        released.fetch_add(1);
      }));
    }
    while (ready.load() != kWaiters) {
      this_thread::yield();
    }
    {
      LockGuard<Mutex> g(m);  // all waiters have subscribed by now
      cv.notify_all();
    }
    for (auto& th : threads) {
      th.join();
    }
    CHECK_EQ(released.load(), kWaiters);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(condvar_wait_for_timeout) {
  Mutex m;
  CondVar cv;
  LockGuard<Mutex> g(m);
  Instant start = Instant::now();
  CHECK(!cv.wait_for(m, 30_ms));
  CHECK(start.elapsed() >= 30_ms);
}

YASL_TEST(condvar_wait_for_woken_returns_true) {
  mem::Tracking t;
  {
    Mutex m;
    CondVar cv;
    Atomic<bool> go{false};

    Thread waker([&] {
      this_thread::sleep_for(10_ms);
      LockGuard<Mutex> g(m);
      go.store(true);
      cv.notify_one();
    });

    {
      LockGuard<Mutex> g(m);
      CHECK(cv.wait_for(m, 500_ms, [&] { return go.load(); }));
    }
    waker.join();
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(condvar_predicate_wait_sees_value) {
  Mutex m;
  CondVar cv;
  Atomic<int> value{0};
  Atomic<int> observed{0};

  Thread waiter([&] {
    LockGuard<Mutex> g(m);
    cv.wait(m, [&] { return value.load() != 0; });
    observed.store(value.load());
  });

  // The waiter holds the mutex until it is inside cv.wait(), so acquiring it
  // guarantees the notify below is not lost.
  {
    LockGuard<Mutex> g(m);
    value.store(42);
    cv.notify_one();
  }
  waiter.join();
  CHECK_EQ(observed.load(), 42);
}

YASL_TEST(condvar_predicate_rechecks_after_spurious_wake) {
  Mutex m;
  CondVar cv;
  Atomic<bool> started{false};
  Atomic<bool> ready{false};
  Atomic<int> pred_calls{0};

  Thread waiter([&] {
    LockGuard<Mutex> g(m);
    started.store(true);
    cv.wait(m, [&] {
      pred_calls.fetch_add(1);
      return ready.load();
    });
  });

  while (!started.load()) {
    this_thread::yield();
  }
  {
    LockGuard<Mutex> g(m);  // waiter has released m inside wait()
    cv.notify_one();        // spurious: the predicate is still false
  }
  this_thread::sleep_for(10_ms);
  {
    LockGuard<Mutex> g(m);
    ready.store(true);
  }
  cv.notify_one();
  waiter.join();
  CHECK(ready.load());
  // At least: one check on entry and one after the value was published.
  CHECK(pred_calls.load() >= 2);
}

YASL_TEST(condvar_wait_for_predicate_timeout_returns_false) {
  Mutex m;
  CondVar cv;
  Atomic<bool> go{false};
  LockGuard<Mutex> g(m);
  Instant start = Instant::now();
  CHECK(!cv.wait_for(m, 30_ms, [&] { return go.load(); }));
  CHECK(start.elapsed() >= 30_ms);
  CHECK(!go.load());
}

YASL_TEST(condvar_guard_overload) {
  // wait(Guard&) works with anything exposing mutex(), e.g. Guarded<T>::Guard.
  Guarded<int> state;
  CondVar cv;
  Thread th([&] {
    this_thread::sleep_for(10_ms);
    {
      auto g = state.lock();
      *g = 7;
    }
    cv.notify_one();
  });
  {
    auto g = state.lock();
    cv.wait(g, [&] { return *g == 7; });
    CHECK_EQ(*g, 7);
  }
  th.join();
}
