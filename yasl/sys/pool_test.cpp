#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/sys/atomic.h>
#include <yasl/sys/pool.h>
#include <yasl/test/test.h>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(waitgroup_and_latch) {
    WaitGroup wg;
    AtomicInt n;
    wg.add(4);
    Vector<Thread> ts;
    for (int i = 0; i < 4; ++i) {
        ts.push_back(Thread([&] {
            this_thread::sleep_for(5_ms);
            n.fetch_add(1);
            wg.done();
        }));
    }
    wg.wait();
    CHECK_EQ(n.load(), 4);
    ts.clear();

    Latch latch(2);
    CHECK(!latch.try_wait());
    Thread t([&] {
        latch.count_down();
        latch.count_down();
    });
    latch.wait();
    CHECK(latch.try_wait());
}

YASL_TEST(pool_spawn_futures) {
    ThreadPool pool(4);
    CHECK_EQ(pool.workers(), size_t(4));
    Vector<Future<long>> fs;
    for (int i = 0; i < 100; ++i) {
        fs.push_back(pool.spawn([i] {
            long s = 0;
            for (int k = 0; k <= i; ++k) {
                s += k;
            }
            return s;
        }));
    }
    long total = 0;
    for (auto& f : fs) {
        total += f.get();
    }
    CHECK_EQ(total, 166650L);

    Future<String> s = pool.spawn([] { return String("done"); });
    CHECK_EQ(s.get(), "done");
    AtomicInt flag;
    Future<void> v = pool.spawn([&] { flag.store(7); });
    v.get();
    CHECK_EQ(flag.load(), 7);
    Future<int> slow = pool.spawn([] {
        this_thread::sleep_for(100_ms);
        return 1;
    });
    CHECK(!slow.wait_for(5_ms));
    CHECK(slow.wait_for(10_s));
    CHECK_EQ(slow.get(), 1);
}

YASL_TEST(pool_runs_queued_tasks_on_destruction) {
    mem::Tracking t;
    AtomicInt n;
    {
        ThreadPool pool(2, &t);
        for (int i = 0; i < 50; ++i) {
            pool.submit([&] {
                this_thread::sleep_for(1_ms);
                n.fetch_add(1);
            });
        }
    }
    CHECK_EQ(n.load(), 50);
    CHECK_EQ(t.stats().live(), size_t(0));
}
