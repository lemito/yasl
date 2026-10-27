#include <yasl/sys/defer.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(defer_order) {
    int log = 0;
    {
        YASL_DEFER { log = log * 10 + 1; };
        YASL_DEFER { log = log * 10 + 2; };
    }
    CHECK_EQ(log, 21);
}

YASL_TEST(defer_dismiss_and_move) {
    int n = 0;
    {
        auto g = scope_exit([&] { ++n; });
        g.dismiss();
    }
    CHECK_EQ(n, 0);
    {
        auto g = scope_exit([&] { ++n; });
        auto h = yasl::move(g);
    }
    CHECK_EQ(n, 1);
}

YASL_TEST(defer_on_throw) {
    int n = 0;
    try {
        YASL_DEFER { ++n; };
        throw 1;
    } catch (int) {
    }
    CHECK_EQ(n, 1);
}
