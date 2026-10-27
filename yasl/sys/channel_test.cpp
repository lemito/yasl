#include <yasl/mem/tracking.h>
#include <yasl/ptr/unique.h>
#include <yasl/str/str.h>
#include <yasl/sys/channel.h>
#include <yasl/sys/thread.h>
#include <yasl/test/test.h>
#include <yasl/vec/vector.h>

#include <atomic>

using namespace yasl;
using namespace yasl::literals;

YASL_TEST(channel_buffered_basic) {
    auto [tx, rx] = make_channel<int>(2);
    CHECK(tx.try_send(1));
    CHECK(tx.try_send(2));
    auto full = tx.try_send(3);
    CHECK(full.is_err());
    CHECK_EQ(full.error(), 3);
    CHECK_EQ(*rx.try_recv(), 1);
    CHECK(tx.send(3));
    CHECK_EQ(*rx.recv(), 2);
    CHECK_EQ(*rx.recv(), 3);
    CHECK(!rx.try_recv().has_value());
    CHECK(!rx.recv_for(5_ms).has_value());
}

YASL_TEST(channel_close_drains_then_none) {
    auto [tx, rx] = make_channel<int>(4);
    tx.send(1);
    tx.send(2);
    tx.close();
    CHECK(tx.is_closed());
    auto r = tx.send(9);
    CHECK(r.is_err() && r.error() == 9);
    CHECK_EQ(*rx.recv(), 1);
    CHECK_EQ(*rx.recv(), 2);
    CHECK(!rx.recv().has_value());
}

YASL_TEST(channel_last_sender_closes) {
    auto [tx, rx] = make_channel<int>(4);
    {
        Sender<int> extra = tx;
        extra.send(7);
    }
    tx.send(8);
    {
        Sender<int> gone = yasl::move(tx);
    }
    int sum = 0;
    for (int v : rx) {
        sum += v;
    }
    CHECK_EQ(sum, 15);
}

YASL_TEST(channel_rendezvous) {
    auto [tx, rx] = make_channel<int>(0);
    CHECK(tx.try_send(1).is_err()); // nobody waiting
    std::atomic<bool> handed{false};
    Thread producer([tx = tx, &handed]() mutable {
        tx.send(42);
        handed = true;
    });
    this_thread::sleep_for(20_ms);
    CHECK(!handed.load()); // still blocked: nobody took it
    CHECK_EQ(*rx.recv(), 42);
    producer.join();
    CHECK(handed.load());
}

YASL_TEST(channel_many_producers_consumers) {
    auto [tx, rx] = make_channel<long>(16);
    constexpr int kProducers = 4;
    constexpr int kConsumers = 3;
    constexpr int kPerProducer = 5000;
    std::atomic<long> total{0};
    std::atomic<long> received{0};
    {
        Vector<Thread> consumers;
        for (int c = 0; c < kConsumers; ++c) {
            consumers.push_back(Thread([rx = rx, &total, &received]() mutable {
                for (long v : rx) {
                    total += v;
                    ++received;
                }
            }));
        }
        {
            Vector<Thread> producers;
            for (int p = 0; p < kProducers; ++p) {
                producers.push_back(Thread([tx = tx]() mutable {
                    for (int i = 1; i <= kPerProducer; ++i) {
                        tx.send(i);
                    }
                }));
            }
            tx = Sender<long>(); // drop ours: channel closes with the last producer
        }
    }
    CHECK_EQ(received.load(), long(kProducers) * kPerProducer);
    CHECK_EQ(total.load(), long(kProducers) * kPerProducer * (kPerProducer + 1) / 2);
}

YASL_TEST(channel_rendezvous_ping_pong) {
    auto [tx, rx] = make_channel<int>(0);
    auto [back_tx, back_rx] = make_channel<int>(0);
    Thread echo([rx = rx, back_tx = back_tx]() mutable {
        for (int v : rx) {
            back_tx.send(v * 2);
        }
    });
    long sum = 0;
    for (int i = 0; i < 1000; ++i) {
        tx.send(i);
        sum += *back_rx.recv();
    }
    tx.close();
    echo.join();
    CHECK_EQ(sum, 2L * (999 * 1000 / 2));
}

YASL_TEST(channel_receivers_gone_unblocks_sender) {
    auto [tx, rx] = make_channel<int>(1);
    tx.send(1);
    Thread blocked([tx = tx]() mutable {
        auto r = tx.send(2); // buffer full: waits until the receiver goes away
        CHECK(r.is_err());
    });
    this_thread::sleep_for(20_ms);
    rx = Receiver<int>();
    blocked.join();
}

YASL_TEST(channel_move_only_and_no_leak) {
    mem::Tracking t;
    {
        auto [tx, rx] = make_channel<Unique<int>>(3, &t);
        tx.send(make_unique_in<int>(&t, 1));
        tx.send(make_unique_in<int>(&t, 2));
        Optional<Unique<int>> first = rx.recv();
        CHECK_EQ(**first, 1);
        // one value stays in the buffer when the channel dies
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}
