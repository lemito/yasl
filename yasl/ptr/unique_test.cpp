#include <yasl/mem/tracking.h>
#include <yasl/ptr/unique.h>
#include <yasl/test/test.h>

using namespace yasl;

namespace {
    int g_live = 0;

    struct Shape {
        virtual ~Shape() = default;
        virtual int area() const = 0;
    };

    struct Square: Shape {
        explicit Square(int s)
            : side(s)
        {
            ++g_live;
        }
        ~Square() override {
            --g_live;
        }
        int area() const override {
            return side * side;
        }
        int side;
        char pad[100];
    };
}

YASL_TEST(unique_basic) {
    mem::Tracking t;
    {
        Unique<int> p = make_unique_in<int>(&t, 5);
        CHECK(p);
        CHECK_EQ(*p, 5);
        CHECK_EQ(t.stats().live(), size_t(1));
        Unique<int> q = yasl::move(p);
        CHECK(p == nullptr);
        CHECK_EQ(*q, 5);
        q.reset();
        CHECK(!q);
        CHECK_EQ(t.stats().live(), size_t(0));
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(unique_upcast) {
    mem::Tracking t;
    {
        Unique<Square> sq = make_unique_in<Square>(&t, 3);
        CHECK_EQ(g_live, 1);
        Unique<Shape> base = yasl::move(sq);
        CHECK(sq == nullptr);
        CHECK_EQ(base->area(), 9);
        Unique<Shape> direct = make_unique_as<Shape, Square>(&t, 4);
        CHECK_EQ(direct->area(), 16);
        CHECK_EQ(g_live, 2);
        base = yasl::move(direct);
        CHECK_EQ(g_live, 1);
        CHECK_EQ(base->area(), 16);
    }
    CHECK_EQ(g_live, 0);
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(unique_release_swap) {
    mem::Tracking t;
    auto a = make_unique_in<int>(&t, 1);
    auto b = make_unique_in<int>(&t, 2);
    a.swap(b);
    CHECK_EQ(*a, 2);
    auto raw = a.release();
    CHECK(!a);
    CHECK_EQ(*raw.first, 2);
    CHECK(raw.second == &t);
    raw.second->destroy(raw.first);
    b.reset();
    CHECK_EQ(t.stats().live(), size_t(0));
    static_assert(sizeof(Unique<int>) == 3 * sizeof(void*));
}
