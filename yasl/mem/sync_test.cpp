#include <yasl/mem/arena.h>
#include <yasl/mem/pool.h>
#include <yasl/mem/sync.h>
#include <yasl/mem/tracking.h>
#include <yasl/conc/mutex.h>
#include <yasl/conc/thread.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::mem;

YASL_TEST(synchronized_allocate_is_thread_safe) {
  Tracking up;
  {
    Synchronized<Pool> pool(&up);
    Vector<void*> ptrs;
    Vector<Thread> threads;
    Mutex ptrs_mutex;
    for (int t = 0; t < 8; ++t) {
      threads.push_back(Thread([&] {
        for (int i = 0; i < 500; ++i) {
          void* p = pool.allocate(32, 8);
          CHECK(p != nullptr);
          LockGuard<Mutex> lock(ptrs_mutex);
          ptrs.push_back(p);
        }
      }));
    }
    for (auto& th : threads) {
      th.join();
    }
    CHECK_EQ(ptrs.size(), size_t(4000));
    for (void* p : ptrs) {
      pool.deallocate(p, 32, 8);
    }
  }
  CHECK_EQ(up.stats().live(), size_t(0));
}

YASL_TEST(synchronized_with_lock_accesses_inner) {
  Synchronized<Pool> pool;
  Pool& p = pool.inner();
  CHECK(p.upstream() == default_resource());
  void* q = pool.with_lock([&](Pool& inner) {
    return inner.allocate(16, 8);
  });
  CHECK(q != nullptr);
  pool.deallocate(q, 16, 8);
}

YASL_TEST(synchronized_resize_under_lock) {
  Synchronized<Arena> arena;
  void* p = arena.allocate(32, 8);
  CHECK(p != nullptr);
  Block b = arena.resize(p, 32, 64, 8, true);
  CHECK(b.ptr != nullptr);
  arena.deallocate(b.ptr ? b.ptr : p, b.size ? b.size : 64, 8);
}
