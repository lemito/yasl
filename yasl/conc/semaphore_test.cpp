#include <yasl/conc/semaphore.h>
#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

#include <atomic>

using namespace yasl;

YASL_TEST(semaphore_basic) {
  Semaphore s(2);
  CHECK(s.try_acquire());
  CHECK(s.try_acquire());
  CHECK(!s.try_acquire());
  s.release();
  CHECK(s.try_acquire());
  s.release(3);
  s.acquire();
  s.acquire();
  s.acquire();
  CHECK(!s.try_acquire());
}

YASL_TEST(semaphore_timeout) {
  Semaphore s;
  Instant t = Instant::now();
  CHECK(!s.acquire_for(Duration::millis(30)));
  CHECK(t.elapsed() >= Duration::millis(25));
  s.release();
  CHECK(s.acquire_for(Duration::millis(30)));
}

YASL_TEST(semaphore_limits_concurrency) {
  Semaphore slots(3);
  std::atomic<int> inside{0};
  std::atomic<int> peak{0};
  {
    Vector<Thread> threads;
    for (int i = 0; i < 8; ++i) {
      threads.push_back(Thread([&] {
        for (int k = 0; k < 50; ++k) {
          slots.acquire();
          int now = ++inside;
          int p = peak.load();
          while (now > p && !peak.compare_exchange_weak(p, now)) {
          }
          this_thread::yield();
          --inside;
          slots.release();
        }
      }));
    }
  }
  CHECK(peak.load() <= 3);
  CHECK(peak.load() >= 1);
}

YASL_TEST(semaphore_wakes_blocked_thread) {
  Semaphore s;
  std::atomic<bool> got{false};
  Thread t([&] {
    s.acquire();
    got = true;
  });
  this_thread::sleep_for(Duration::millis(20));
  CHECK(!got.load());
  s.release();
  t.join();
  CHECK(got.load());
}

YASL_TEST(barrier_rounds) {
  constexpr int kThreads = 4;
  constexpr int kRounds = 100;
  Barrier barrier(kThreads);
  std::atomic<int> arrived[kRounds] = {};
  std::atomic<int> leaders{0};
  std::atomic<bool> ok{true};
  {
    Vector<Thread> threads;
    for (int i = 0; i < kThreads; ++i) {
      threads.push_back(Thread([&] {
        for (int r = 0; r < kRounds; ++r) {
          ++arrived[r];
          if (barrier.wait()) {
            ++leaders;
          }
          // After the barrier, everybody of this round must have arrived.
          if (arrived[r].load() != kThreads) {
            ok = false;
          }
        }
      }));
    }
  }
  CHECK(ok.load());
  CHECK_EQ(leaders.load(), kRounds);
}
