#include <yasl/conc/once.h>
#include <yasl/conc/thread.h>
#include <yasl/mem/tracking.h>
#include <yasl/test/test.h>

#include <stdexcept>
#include "yasl/conc/atomic.h"
#include "yasl/cont/seq/vector.h"

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(once_call_once_runs_once) {
  mem::Tracking t;
  {
    Once once;
    int calls = 0;
    once.call_once([&] { ++calls; });
    once.call_once([&] { ++calls; });
    once.call_once([&] { ++calls; });
    CHECK_EQ(calls, 1);
    CHECK(once.is_done());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(once_exception_safety_retries) {
  Once once;
  int attempts = 0;
  bool threw = false;
  for (int i = 0; i < 3; ++i) {
    try {
      once.call_once([&] {
        ++attempts;
        if (attempts < 3) {
          throw std::runtime_error("retry me");
        }
      });
    } catch (const std::runtime_error&) {
      threw = true;
    }
  }
  CHECK(threw);
  CHECK_EQ(attempts, 3);
  CHECK(once.is_done());
}

YASL_TEST(once_concurrent_init_single_initializer) {
  mem::Tracking t;
  {
    Once once;
    Atomic<int> inits{0};
    Atomic<int> max_concurrent{0};
    Atomic<int> current{0};

    Vector<Thread> threads;
    for (int i = 0; i < 16; ++i) {
      threads.push_back(Thread([&] {
        once.call_once([&] {
          int c = current.fetch_add(1) + 1;
          int prev = max_concurrent.load();
          while (c > prev &&
                 max_concurrent.compare_exchange_weak(prev, c)) {
          }
          this_thread::sleep_for(100_us);
          current.fetch_sub(1);
          inits.fetch_add(1);
        });
      }));
    }
    for (auto& th : threads) {
      th.join();
    }
    CHECK_EQ(inits.load(), 1);
    CHECK(once.is_done());
    CHECK(max_concurrent.load() >= 1);
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(once_lock_get_or_init_and_get) {
  OnceLock<int> lock;
  CHECK(!lock.is_set());
  CHECK(lock.get() == none);
  CHECK_EQ(lock.get_or_init([] { return 42; }), 42);
  CHECK(lock.is_set());
  CHECK_EQ(*lock.get(), 42);
  // Second call does not re-run the initializer.
  int init_count = 0;
  auto init = [&] {
    ++init_count;
    return 7;
  };
  OnceLock<int> lazy;
  lazy.get_or_init(init);
  lazy.get_or_init(init);
  CHECK_EQ(init_count, 1);
}

YASL_TEST(once_lock_move_only_value) {
  struct MoveOnly {
    int v;
    explicit MoveOnly(int x) : v(x) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
  };
  OnceLock<MoveOnly> lock;
  MoveOnly& ref = lock.get_or_init([] { return MoveOnly{99}; });
  CHECK_EQ(ref.v, 99);
  CHECK(lock.is_set());
  CHECK_EQ((*lock.get()).v, 99);
}

YASL_TEST(lazy_computes_on_first_use) {
  int calls = 0;
  auto l = lazy([&] {
    ++calls;
    return 5;
  });
  CHECK(!l.is_initialized());
  CHECK_EQ(*l, 5);
  CHECK_EQ(calls, 1);
  CHECK_EQ(*l, 5);
  CHECK_EQ(calls, 1);  // not recomputed
  CHECK(l.is_initialized());
}
