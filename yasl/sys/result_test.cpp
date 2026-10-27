#include <yasl/io/file.h>
#include <yasl/mem/tracking.h>
#include <yasl/str/str.h>
#include <yasl/sys/result.h>
#include <yasl/test/test.h>

using namespace yasl;

namespace {
    Result<int, int> parse(int x) {
        if (x < 0) {
            return err(x);
        }
        return ok(x * 2);
    }

    Result<int, int> chain(int a, int b) {
        int x = YASL_TRY(parse(a));
        int y = YASL_TRY(parse(b));
        return ok(x + y);
    }

    Result<void, int> check_void(int a) {
        YASL_TRY_VOID(parse(a));
        return ok();
    }

    struct Counted {
        explicit Counted(mem::Tracking* t)
            : t_(t)
            , p_(t->allocate(8, 8))
        {
        }
        Counted(Counted&& o) noexcept
            : t_(o.t_)
            , p_(yasl::exchange(o.p_, nullptr))
        {
        }
        ~Counted() {
            if (p_) {
                t_->deallocate(p_, 8, 8);
            }
        }
        mem::Tracking* t_;
        void* p_;
    };
}

YASL_TEST(result_basic) {
    Result<int, int> a = ok(3);
    CHECK(a);
    CHECK(a.is_ok());
    CHECK_EQ(*a, 3);
    CHECK_EQ(a.value(), 3);
    Result<int, int> b = err(7);
    CHECK(b.is_err());
    CHECK_EQ(b.error(), 7);
    CHECK_EQ(b.value_or(5), 5);
    CHECK_THROWS(b.value(), BadAccess);
    auto c = b;
    CHECK_EQ(c.error(), 7);
    c = a;
    CHECK_EQ(*c, 3);
}

YASL_TEST(result_combinators) {
    auto r = parse(4).map([](int v) { return v + 1; });
    CHECK_EQ(*r, 9);
    auto e = parse(-1).map_err([](int v) { return v * 10; });
    CHECK_EQ(e.error(), -10);
    auto t = parse(2).and_then([](int v) { return parse(v - 10); });
    CHECK_EQ(t.error(), -6);
    auto o = parse(-2).or_else([](int) { return parse(1); });
    CHECK_EQ(*o, 2);
    CHECK_EQ(*parse(1).ok(), 2);
    CHECK(!parse(-1).ok().has_value());
}

YASL_TEST(result_try) {
    CHECK_EQ(*chain(1, 2), 6);
    CHECK_EQ(chain(1, -2).error(), -2);
    CHECK_EQ(chain(-5, 2).error(), -5);
    CHECK(check_void(1));
    CHECK_EQ(check_void(-3).error(), -3);
}

YASL_TEST(result_void) {
    Result<void, String> r = ok();
    CHECK(r);
    r.value();
    Result<void, String> e = err(String("bad"));
    CHECK(!e);
    CHECK_EQ(e.error(), "bad");
    CHECK_THROWS(e.value(), BadAccess);
}

YASL_TEST(result_move_only_no_leak) {
    mem::Tracking t;
    {
        Result<Counted, int> a = ok(Counted(&t));
        CHECK_EQ(t.stats().live(), size_t(1));
        Result<Counted, int> b = yasl::move(a);
        CHECK_EQ(t.stats().live(), size_t(1));
        Result<Counted, int> c = err(1);
        c = yasl::move(b);
        CHECK(c);
    }
    CHECK_EQ(t.stats().live(), size_t(0));
}

YASL_TEST(result_io_error) {
    Result<int, io::Error> r = err(io::Error("open", "x", 2));
    auto copy = r;
    CHECK_EQ(copy.error().code(), 2);
}
