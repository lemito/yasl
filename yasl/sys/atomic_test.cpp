#include <yasl/sys/atomic.h>
#include <yasl/sys/thread.h>
#include <yasl/test/test.h>
#include <yasl/vec/vector.h>

using namespace yasl;

YASL_TEST(atomic_basic) {
    Atomic<int> n{5};
    CHECK_EQ(n.load(), 5);
    CHECK_EQ(n.fetch_add(3, Order::Relaxed), 5);
    CHECK_EQ(n.exchange(1), 8);
    int expected = 2;
    CHECK(!n.compare_exchange(expected, 9));
    CHECK_EQ(expected, 1);
    CHECK(n.compare_exchange(expected, 9, Order::AcqRel, Order::Acquire));
    CHECK_EQ(n.load(Order::Acquire), 9);
    CHECK_EQ(n.fetch_or(6), 9);
    CHECK_EQ(n.load(), 15);
    CHECK_EQ(n.update([](int v) { return v * 2; }), 15);
    CHECK_EQ(n.load(), 30);
    AtomicBool flag;
    CHECK(!flag.load());
    flag.store(true, Order::Release);
    CHECK(flag.load(Order::Acquire));
}

YASL_TEST(atomic_threads) {
    AtomicU64 total;
    {
        Vector<Thread> ts;
        for (int i = 0; i < 4; ++i) {
            ts.push_back(Thread([&] {
                for (int k = 0; k < 20000; ++k) {
                    total.fetch_add(1, Order::Relaxed);
                }
            }));
        }
    }
    CHECK_EQ(total.load(), uint64(80000));
}
