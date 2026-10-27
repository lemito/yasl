#include <yasl/mem/tracking.h>
#include <yasl/ptr/shared.h>
#include <yasl/test/test.h>

#include <string>
#include <thread>
#include <vector>

using namespace yasl;

YASL_TEST(shared_basic) {
    mem::Tracking t;
    {
        Shared<std::string> a = make_shared_in<std::string>(&t, "value");
        CHECK_EQ(*a, "value");
        CHECK_EQ(a->size(), size_t(5));
        CHECK_EQ(a.use_count(), size_t(1));
        CHECK_EQ(t.stats().live(), size_t(1));

        Shared<std::string> b = a;
        CHECK(a == b);
        CHECK_EQ(a.use_count(), size_t(2));
        CHECK(!a.unique());

        Shared<std::string> c = std::move(b);
        CHECK(b == nullptr);
        CHECK_EQ(a.use_count(), size_t(2));

        c.reset();
        CHECK(a.unique());
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(shared_make_mut) {
    auto a = make_shared<std::vector<int>>(3, 1);
    auto b = a;
    b.make_mut().push_back(2);
    CHECK_EQ(a->size(), size_t(3));
    CHECK_EQ(b->size(), size_t(4));
    CHECK(a.unique() && b.unique());

    std::vector<int>* before = a.get();
    a.make_mut().push_back(5);
    CHECK_EQ(a.get(), before);
}

YASL_TEST(shared_threads) {
    mem::Tracking t;
    {
        auto shared = make_shared_in<std::vector<int>>(&t, 100, 7);
        std::vector<std::thread> threads;
        for (int k = 0; k < 4; ++k) {
            threads.emplace_back([shared] {
                for (int i = 0; i < 10000; ++i) {
                    Shared<std::vector<int>> local = shared;
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

namespace {
    struct Base {
        virtual ~Base() = default;
        virtual int id() const {
            return 1;
        }
    };

    struct Derived: Base {
        int id() const override {
            return 2;
        }
    };

    struct Node {
        Weak<Node> parent;
        Shared<Node> child;
    };
}

YASL_TEST(shared_upcast) {
    mem::Tracking t;
    {
        Shared<Derived> d = make_shared_in<Derived>(&t);
        Shared<Base> b = d;
        CHECK_EQ(b->id(), 2);
        CHECK_EQ(d.use_count(), size_t(2));
        Weak<Base> w = d;
        d.reset();
        b.reset();
        CHECK(w.expired());
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(shared_weak_lock) {
    mem::Tracking t;
    Weak<int> w;
    {
        Shared<int> s = make_shared_in<int>(&t, 5);
        w = s;
        CHECK_EQ(s.weak_count(), size_t(1));
        auto l = w.lock();
        CHECK(l);
        CHECK_EQ(*l, 5);
        CHECK_EQ(s.use_count(), size_t(2));
    }
    CHECK(w.expired());
    CHECK(!w.lock());
    // value is gone but the block lives until the last Weak
    CHECK_EQ(t.stats().live(), size_t(1));
    w.reset();
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(shared_cycle_with_weak) {
    mem::Tracking t;
    {
        auto parent = make_shared_in<Node>(&t);
        parent->child = make_shared_in<Node>(&t);
        parent->child->parent = parent;
        CHECK_EQ(parent.use_count(), size_t(1));
        CHECK(parent->child->parent.lock() == parent);
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(shared_from_unique) {
    mem::Tracking t;
    {
        auto u = make_unique_in<Derived>(&t);
        Unique<Base> ub = yasl::move(u);
        Shared<Base> s = Shared<Base>::from(yasl::move(ub));
        CHECK(!ub);
        CHECK_EQ(s->id(), 2);
        CHECK_EQ(t.stats().live(), size_t(2));
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(shared_weak_threads) {
    mem::Tracking t;
    {
        auto s = make_shared_in<int>(&t, 1);
        Weak<int> w = s;
        std::vector<std::thread> threads;
        for (int k = 0; k < 4; ++k) {
            threads.emplace_back([w] {
                for (int i = 0; i < 20000; ++i) {
                    if (auto l = w.lock()) {
                        if (*l != 1) {
                            std::abort();
                        }
                    }
                }
            });
        }
        s.reset();
        for (auto& th : threads) {
            th.join();
        }
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(shared_make_default) {
    Shared<int> s = make_shared<int>(1);
    CHECK_EQ(*s, 1);
}
