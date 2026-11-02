#include <yasl/conc/atomic.h>
#include <yasl/conc/mutex.h>
#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(mutex_lock_unlock) {
  Mutex m;
  m.lock();
  CHECK(!m.try_lock());  // not recursive
  m.unlock();
  CHECK(m.try_lock());
  m.unlock();
}

YASL_TEST(mutex_try_lock_reflects_ownership) {
  Mutex m;
  m.lock();
  CHECK(!m.try_lock());
  CHECK(!m.try_lock());
  m.unlock();
  CHECK(m.try_lock());
  CHECK(!m.try_lock());
  m.unlock();
}

YASL_TEST(mutex_lock_guard_raii) {
  Mutex m;
  {
    LockGuard<Mutex> g(m);
    CHECK(!m.try_lock());  // held by the guard
  }
  CHECK(m.try_lock());  // released when the guard died
  m.unlock();
}

YASL_TEST(mutex_lock_guard_independent_mutexes) {
  Mutex a;
  Mutex b;
  {
    LockGuard<Mutex> ga(a);
    CHECK(!a.try_lock());
    {
      LockGuard<Mutex> gb(b);
      CHECK(!b.try_lock());
      CHECK(!a.try_lock());  // ga still holds a
    }
    CHECK(b.try_lock());
    b.unlock();
    CHECK(!a.try_lock());
  }
  CHECK(a.try_lock());
  a.unlock();
  CHECK(b.try_lock());
  b.unlock();
}

YASL_TEST(mutex_contention_protects_counter) {
  mem::Tracking t;
  {
    Mutex m;
    long n = 0;
    Vector<Thread> threads;
    for (int i = 0; i < 8; ++i) {
      threads.push_back(Thread([&] {
        for (int k = 0; k < 1000; ++k) {
          LockGuard<Mutex> l(m);
          n++;
        }
      }));
    }
    for (auto& th : threads) {
      th.join();
    }
    CHECK_EQ(n, 8000L);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(mutex_try_lock_until_acquired) {
  Mutex m;
  Atomic<bool> acquired{false};

  Thread holder([&] {
    m.lock();
    this_thread::sleep_for(5_ms);
    m.unlock();
  });

  // Fairness is not guaranteed, but try_lock must eventually succeed once the
  // holder releases (otherwise this is starvation).
  Instant start = Instant::now();
  while (!acquired.load() && start.elapsed() < 5_s) {
    if (m.try_lock()) {
      acquired.store(true);
      m.unlock();
      break;
    }
    this_thread::yield();
  }
  holder.join();
  CHECK(acquired.load());
}

YASL_TEST(mutex_no_starvation_under_contention) {
  Mutex m;
  long served = 0;
  constexpr int kThreads = 8;
  constexpr int kIterations = 500;

  Instant start = Instant::now();
  {
    Vector<Thread> threads;
    for (int i = 0; i < kThreads; ++i) {
      threads.push_back(Thread([&] {
        for (int k = 0; k < kIterations; ++k) {
          LockGuard<Mutex> g(m);
          ++served;
        }
      }));
    }
    for (auto& th : threads) {
      th.join();
    }
  }
  CHECK_EQ(served, long(kThreads * kIterations));
  CHECK(start.elapsed() < 30_s);  // every thread made progress
}
