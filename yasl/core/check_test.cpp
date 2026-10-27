#include <yasl/core/check.h>
#include <yasl/test/test.h>

using namespace yasl;

namespace {
PanicInfo g_seen{};

void recording_hook(const PanicInfo& info) {
  g_seen = info;
  throw test::Panicked{};
}
}  // namespace

YASL_TEST(check_verify_and_insist_pass_silently) {
  YASL_VERIFY(1 + 1 == 2);
  YASL_INSIST(true, "never printed");
  YASL_ASSERT(true);
}

YASL_TEST(check_verify_panics) {
  int x = 3;
  CHECK_PANICS(YASL_VERIFY(x == 4));
  CHECK_PANICS(YASL_INSIST(x == 4, "x must be four"));
  CHECK_PANICS(YASL_PANIC("boom"));
}

YASL_TEST(check_assert_panics_in_debug) {
#if defined(NDEBUG) && !defined(YASL_FORCE_ASSERTS)
  YASL_ASSERT(false);  // compiled out
#else
  CHECK_PANICS(YASL_ASSERT(false));
#endif
}

YASL_TEST(check_hook_receives_details) {
  PanicHook prev = set_panic_hook(&recording_hook);
  try {
    YASL_INSIST(2 > 3, "two is not greater");
  } catch (const test::Panicked&) {
  }
  set_panic_hook(prev);
  CHECK_EQ(StringView(g_seen.kind), "insist failed");
  CHECK_EQ(StringView(g_seen.expr), "2 > 3");
  CHECK_EQ(StringView(g_seen.message), "two is not greater");
  CHECK(g_seen.line > 0);
  CHECK(StringView(g_seen.file).ends_with("check_test.cpp"));
  CHECK_EQ(StringView(g_seen.func), "yasl_test_check_hook_receives_details");
}

YASL_TEST(check_set_hook_returns_previous) {
  PanicHook a = set_panic_hook(&recording_hook);
  PanicHook b = set_panic_hook(a);
  CHECK(b == &recording_hook);
}

YASL_TEST(check_panic_does_not_return_from_hook_that_throws) {
  bool after = false;
  auto body = [&] {
    YASL_PANIC("stop");
    after = true;
  };
  CHECK_PANICS(body());
  CHECK(!after);
}
