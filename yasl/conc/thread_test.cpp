#include <yasl/conc/atomic.h>
#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

#include "yasl/conc/mutex.h"

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(thread_join_and_destructor) {
  yasl::Atomic<int> n{0};
  {
    Thread a([&] { n.fetch_add(1); });
    Thread b([&] { n.fetch_add(10); });
    CHECK(a.joinable());
    a.join();
    CHECK(!a.joinable());
    a.join();  // harmless
  }
  CHECK_EQ(n.load(), 11);
}

YASL_TEST(thread_many_and_move) {
  Atomic<int> n{0};
  {
    Vector<Thread> threads;
    for (int i = 0; i < 8; ++i) {
      threads.push_back(Thread([&] {
        for (int k = 0; k < 1000; ++k) {
          n.fetch_add(1);
        }
      }));
    }
    Thread moved = yasl::move(threads[0]);
    CHECK(!threads[0].joinable());
    CHECK(moved.joinable());
  }
  CHECK_EQ(n.load(), 8000);
}

YASL_TEST(thread_big_capture_no_leak) {
  mem::Tracking t;
  Atomic<long> sum{0};
  {
    long pad[16] = {1, 2, 3};
    Thread th([&sum, pad] { sum.store(pad[0] + pad[1] + pad[2]); }, &t);
    th.join();
    CHECK_EQ(sum.load(), 6L);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(thread_detach_and_sleep) {
  Atomic<bool> done{false};
  Thread th([&] {
    this_thread::sleep_for(5_ms);
    done.store(true);
  });
  th.detach();
  CHECK(!th.joinable());
  Instant start = Instant::now();
  while (!done.load()) {
    this_thread::yield();
    CHECK(start.elapsed() < 10_s);
  }
  Instant s = Instant::now();
  this_thread::sleep_for(20_ms);
  CHECK(s.elapsed() >= 20_ms);
  CHECK(Thread::hardware_concurrency() >= 1u);
  CHECK(this_thread::id() != 0);
}

YASL_TEST(thread_vector) {
  Vector<Thread> threads;
  yasl::Mutex m;
  long n = 0;
  for (int i = 0; i < 8; ++i) {
    threads.push_back(Thread([&] {
      for (int k = 0; k < 1000; ++k) {
        yasl::LockGuard<yasl::Mutex> l(m);
        n++;
      }
    }));
  }
}