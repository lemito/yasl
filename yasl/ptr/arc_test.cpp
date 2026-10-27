#include <yasl/mem/tracking.h>
#include <yasl/ptr/arc.h>
#include <yasl/test/test.h>

#include <string>
#include <thread>
#include <vector>

using namespace yasl;

YASL_TEST(arc_basic) {
    mem::Tracking t;
    {
        Arc<std::string> a = make_arc_in<std::string>(&t, "value");
        CHECK_EQ(*a, "value");
        CHECK_EQ(a->size(), size_t(5));
        CHECK_EQ(a.use_count(), size_t(1));
        CHECK_EQ(t.stats().live(), size_t(1));

        Arc<std::string> b = a;
        CHECK(a == b);
        CHECK_EQ(a.use_count(), size_t(2));
        CHECK(!a.unique());

        Arc<std::string> c = std::move(b);
        CHECK(b == nullptr);
        CHECK_EQ(a.use_count(), size_t(2));

        c.reset();
        CHECK(a.unique());
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(arc_make_mut) {
    auto a = make_arc<std::vector<int>>(3, 1);
    auto b = a;
    b.make_mut().push_back(2);
    CHECK_EQ(a->size(), size_t(3));
    CHECK_EQ(b->size(), size_t(4));
    CHECK(a.unique() && b.unique());

    std::vector<int>* before = a.get();
    a.make_mut().push_back(5);
    CHECK_EQ(a.get(), before);
}

YASL_TEST(arc_threads) {
    mem::Tracking t;
    {
        auto shared = make_arc_in<std::vector<int>>(&t, 100, 7);
        std::vector<std::thread> threads;
        for (int k = 0; k < 4; ++k) {
            threads.emplace_back([shared] {
                for (int i = 0; i < 10000; ++i) {
                    Arc<std::vector<int>> local = shared;
                    if ((*local)[0] != 7) {
                        std::abort();
                    }
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        CHECK(shared.unique());
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}
