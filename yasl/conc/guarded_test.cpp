#include <yasl/conc/condvar.h>
#include <yasl/conc/guarded.h>
#include <yasl/conc/once.h>
#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/test/test.h>

#include <atomic>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(guarded_counter_threads) {
  Guarded<long> counter;
  {
    Vector<Thread> threads;
    for (int i = 0; i < 4; ++i) {
      threads.push_back(Thread([&] {
        for (int k = 0; k < 10000; ++k) {
          *counter.lock() += 1;
        }
      }));
    }
  }
  CHECK_EQ(*counter.lock(), 40000L);
  auto g = counter.lock();
  CHECK(!counter.try_lock().has_value());
  (void)g;
}

YASL_TEST(guarded_arrow_and_try_lock) {
  Guarded<Vector<int>> v;
  v.lock()->push_back(3);
  {
    auto t = v.try_lock();
    CHECK(t.has_value());
    CHECK_EQ((**t).size(), size_t(1));
  }
  CHECK(v.try_lock().has_value());
}

YASL_TEST(rwlock_readers_writer) {
  RwLock<int> lock;
  {
    auto r1 = lock.read();
    auto r2 = lock.read();
    CHECK(!lock.try_write().has_value());
    CHECK_EQ(*r1, 0);
    (void)r2;
  }
  {
    auto w = lock.write();
    *w = 5;
    CHECK(!lock.try_read().has_value());
  }
  CHECK_EQ(*lock.read(), 5);
}

YASL_TEST(condvar_producer_consumer) {
  Guarded<Vector<int>> queue;
  CondVar cv;
  long sum = 0;
  Thread consumer([&] {
    for (int got = 0; got < 100;) {
      auto g = queue.lock();
      cv.wait(g, [&] { return !g->empty(); });
      for (int x : *g) {
        sum += x;
        ++got;
      }
      g->clear();
    }
  });
  for (int i = 1; i <= 100; ++i) {
    queue.lock()->push_back(i);
    cv.notify_one();
  }
  consumer.join();
  CHECK_EQ(sum, 5050L);
}

YASL_TEST(condvar_timeout) {
  Guarded<int> state;
  CondVar cv;
  auto g = state.lock();
  Instant start = Instant::now();
  CHECK(!cv.wait_for(g, 20_ms, [] { return false; }));
  CHECK(start.elapsed() >= 20_ms);
  CHECK(!cv.wait_for(g.mutex(), 5_ms));
}

YASL_TEST(once_and_lazy) {
  std::atomic<int> runs{0};
  Once once;
  {
    Vector<Thread> threads;
    for (int i = 0; i < 8; ++i) {
      threads.push_back(
          Thread([&] { once.call_once([&] { runs.fetch_add(1); }); }));
    }
  }
  CHECK_EQ(runs.load(), 1);
  CHECK(once.is_done());

  int built = 0;
  auto table = lazy([&] {
    ++built;
    return String("computed");
  });
  CHECK(!table.is_initialized());
  CHECK_EQ(*table, "computed");
  CHECK_EQ(table->size(), size_t(8));
  CHECK_EQ(built, 1);
}

YASL_TEST(once_lock_retry_after_throw) {
  mem::Tracking t;
  {
    OnceLock<String> cell;
    CHECK(!cell.get().has_value());
    CHECK_THROWS(
        cell.get_or_init([]() -> String { throw OutOfRange("init failed"); }),
        OutOfRange);
    CHECK(!cell.is_set());
    String& s = cell.get_or_init([&] {
      return String("a value long enough to need a heap block....", &t);
    });
    CHECK(cell.is_set());
    CHECK_EQ(&s, &*cell.get());
  }
  CHECK_EQ(t.stats().live(), size_t(0));
}
