#include <yasl/conc/atomic.h>
#include <yasl/conc/futex.h>
#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::literals;

// The waiting threads use futex_wait_for() with a generous timeout so that a
// lost wake-up fails the check instead of hanging the whole test binary.

YASL_TEST(futex_wait_wake_one) {
  std::atomic<uint32> word{0};
  Atomic<int> arrived{0};
  Atomic<int> woken{0};

  Thread waiter([&] {
    arrived.fetch_add(1);
    if (detail::futex_wait_for(word, 0, 2_s)) {
      woken.fetch_add(1);
    }
  });

  while (arrived.load() == 0) {
    this_thread::yield();
  }
  this_thread::sleep_for(20_ms);  // give the waiter time to block
  detail::futex_wake_one(word);
  waiter.join();
  CHECK_EQ(woken.load(), 1);
}

YASL_TEST(futex_wake_one_wakes_single_waiter) {
  mem::Tracking t;
  {
    std::atomic<uint32> word{0};
    Atomic<int> arrived{0};
    Atomic<int> woken{0};
    constexpr int kWaiters = 4;

    Vector<Thread> threads;
    for (int i = 0; i < kWaiters; ++i) {
      threads.push_back(Thread([&] {
        arrived.fetch_add(1);
        if (detail::futex_wait_for(word, 0, 2_s)) {
          woken.fetch_add(1);
        }
      }));
    }
    while (arrived.load() != kWaiters) {
      this_thread::yield();
    }
    this_thread::sleep_for(30_ms);

    detail::futex_wake_one(word);
    this_thread::sleep_for(50_ms);
    CHECK_EQ(woken.load(), 1);  // exactly one, not all

    detail::futex_wake_all(word);
    for (auto& th : threads) {
      th.join();
    }
    CHECK_EQ(woken.load(), kWaiters);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(futex_wait_for_timeout) {
  std::atomic<uint32> word{0};
  Instant start = Instant::now();
  CHECK(!detail::futex_wait_for(word, 0, 20_ms));
  CHECK(start.elapsed() >= 20_ms);
}

YASL_TEST(futex_wait_for_zero_timeout_returns_immediately) {
  std::atomic<uint32> word{0};
  CHECK(!detail::futex_wait_for(word, 0, Duration()));
}

YASL_TEST(futex_wait_for_woken_returns_true) {
  std::atomic<uint32> word{0};

  Thread waker([&] {
    this_thread::sleep_for(10_ms);
    word.store(1);
    detail::futex_wake_all(word);
  });

  // True whether the waiter blocked and was woken or the word already moved.
  CHECK(detail::futex_wait_for(word, 0, 2_s));
  waker.join();
  CHECK_EQ(word.load(), 1u);
}

YASL_TEST(futex_wait_returns_when_word_differs) {
  // A wait whose expected value no longer matches must not block at all.
  std::atomic<uint32> word{5};
  Instant start = Instant::now();
  detail::futex_wait(word, 0);
  CHECK(start.elapsed() < 1_s);
}

YASL_TEST(futex_generation_loop) {
  // A wake-up does not change the word, so every waiter must re-check the
  // word and re-arm the wait for the next generation.
  std::atomic<uint32> word{0};
  Atomic<int> passes{0};
  constexpr uint32 kSteps = 3;

  Thread waiter([&] {
    for (uint32 i = 1; i <= kSteps; ++i) {
      detail::futex_wait(word, i - 1);
      passes.fetch_add(1);
    }
  });
  for (uint32 i = 1; i <= kSteps; ++i) {
    this_thread::sleep_for(5_ms);
    word.store(i);
    detail::futex_wake_all(word);
  }
  waiter.join();
  CHECK_EQ(passes.load(), int(kSteps));
  CHECK_EQ(word.load(), kSteps);
}

YASL_TEST(futex_many_waiters_wake_all) {
  mem::Tracking t;
  {
    std::atomic<uint32> word{0};
    Atomic<int> arrived{0};
    Atomic<int> woken{0};
    constexpr int kWaiters = 8;

    Vector<Thread> threads;
    for (int i = 0; i < kWaiters; ++i) {
      threads.push_back(Thread([&] {
        arrived.fetch_add(1);
        if (detail::futex_wait_for(word, 0, 2_s)) {
          woken.fetch_add(1);
        }
      }));
    }
    while (arrived.load() != kWaiters) {
      this_thread::yield();
    }
    this_thread::sleep_for(30_ms);
    detail::futex_wake_all(word);
    for (auto& th : threads) {
      th.join();
    }
    CHECK_EQ(woken.load(), kWaiters);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
